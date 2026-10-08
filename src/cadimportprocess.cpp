#include "cadimportprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr qint64 maximumInput = 100 * 1024 * 1024;
constexpr qint64 maximumOutput = 64 * 1024 * 1024;
constexpr qint64 maximumLogs = 128 * 1024;
#ifdef Q_OS_WIN
// Job在CreateProcess时附加，worker没有先运行再被限制的窗口。
struct WindowsJob {
    HANDLE handle = nullptr;
    STARTUPINFOEXW startup{};
    std::vector<unsigned char> attributes;
    bool initialized = false;
    ~WindowsJob() {
        if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (handle) CloseHandle(handle);
    }
    bool prepare() {
        handle = CreateJobObjectW(nullptr, nullptr);
        if (!handle) return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            | JOB_OBJECT_LIMIT_JOB_MEMORY;
        limits.JobMemoryLimit = SIZE_T(2ULL * 1024 * 1024 * 1024);
        if (!SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            return false;
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (!bytes) return false;
        attributes.resize(bytes);
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) return false;
        initialized = true;
        return UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                        &handle, sizeof(handle), nullptr, nullptr);
    }
};
#endif
}

struct CadImportProcess::State {
    Config config;
    QProcess process;
    QTimer deadline;
    QTimer outputWatch;
    std::unique_ptr<QTemporaryDir> directory;
    Completion completion;
    QString output;
    QString error;
    QString stopError;
    qint64 logs = 0;
    quint64 generation = 0;
    bool busy = false;
    bool stopping = false;
    bool confined = false;
#ifdef Q_OS_WIN
    std::unique_ptr<WindowsJob> job;
#endif
};

CadImportProcess::Config CadImportProcess::Config::runtime()
{
    const QDir root(QCoreApplication::applicationDirPath() + QStringLiteral("/cad-runtime"));
    Config config;
    config.python = qEnvironmentVariable("EQUIPMENT_CAD_PYTHON", root.filePath(QStringLiteral("python.exe")));
    config.modules = qEnvironmentVariable("EQUIPMENT_CAD_MODULES", root.filePath(QStringLiteral("site-packages")));
    config.converter = qEnvironmentVariable("EQUIPMENT_CAD_CONVERTER", root.filePath(QStringLiteral("convert_step.py")));
    return config;
}

CadImportProcess::CadImportProcess(QObject *parent) : CadImportProcess(Config::runtime(), parent) {}

CadImportProcess::CadImportProcess(const Config &config, QObject *parent)
    : QObject(parent), state_(std::make_unique<State>())
{
    state_->config = config;
    state_->config.timeoutMs = std::clamp(config.timeoutMs, 50, 120000);
    state_->deadline.setSingleShot(true);
    state_->outputWatch.setInterval(100);
    connect(&state_->deadline, &QTimer::timeout, this, [this] {
        stop(QStringLiteral("CAD转换超过等待期限，已取消；原设备保留。"));
    });
    connect(&state_->outputWatch, &QTimer::timeout, this, [this] {
        if (QFileInfo(state_->output).size() > maximumOutput)
            stop(QStringLiteral("CAD结果超过64 MiB，已取消；原设备保留。"));
    });
}

CadImportProcess::~CadImportProcess()
{
    // QObject基类析构晚于派生成员；先清回调，避免取消信号访问已销毁窗口。
    state_->completion = {};
    cancel();
    if (state_->busy && state_->directory)
        state_->directory->setAutoRemove(false);
}

bool CadImportProcess::isBusy() const { return state_->busy; }
qint64 CadImportProcess::processId() const { return state_->process.processId(); }
bool CadImportProcess::isConfined() const { return state_->confined; }
QString CadImportProcess::lastError() const { return state_->error; }

bool CadImportProcess::start(const QString &input, Completion completion)
{
    auto &s = *state_;
    if (s.busy) { s.error = QStringLiteral("CAD转换正在进行，请等待或取消后再导入。"); return false; }
    s.error.clear();
    const QFileInfo source(input);
    const QString suffix = source.suffix().toLower();
    if ((suffix != QStringLiteral("step") && suffix != QStringLiteral("stp"))
        || !source.isFile() || !source.isReadable() || source.size() <= 0 || source.size() > maximumInput) {
        s.error = QStringLiteral("请选择非空且不超过100 MiB的STEP/STP文件。"); return false;
    }
    const Config &config = s.config;
    for (const QString &path : {config.python, config.modules, config.converter}) {
        if (!QFileInfo(path).isAbsolute()) {
            s.error = QStringLiteral("CAD运行路径必须为显式受信绝对路径。"); return false;
        }
    }
    if (!QFileInfo(config.python).isFile() || !QFileInfo(config.converter).isFile()
        || !QFileInfo(config.modules).isDir()) {
        s.error = QStringLiteral("CAD运行库不完整，请检查cad-runtime或显式CAD运行路径。"); return false;
    }
    s.directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/equipment-cad-XXXXXX"));
    if (!s.directory->isValid()) {
        s.error = QStringLiteral("无法创建CAD临时结果目录。"); s.directory.reset(); return false;
    }
#ifdef Q_OS_WIN
    s.job = std::make_unique<WindowsJob>();
    if (!s.job->prepare()) {
        s.error = QStringLiteral("无法建立CAD进程资源约束，未启动转换。");
        s.job.reset(); s.directory.reset(); return false;
    }
    s.process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args) {
        auto &job = *state_->job;
        job.startup.StartupInfo = *reinterpret_cast<STARTUPINFOW *>(args->startupInfo);
        job.startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        args->startupInfo = reinterpret_cast<Q_STARTUPINFO *>(&job.startup);
        args->flags |= EXTENDED_STARTUPINFO_PRESENT;
    });
#endif
    s.output = s.directory->filePath(QStringLiteral("result.json"));
    s.logs = 0; s.stopError.clear(); s.stopping = false; s.confined = false;
    s.completion = std::move(completion);
    s.busy = true;
    const quint64 operation = ++s.generation;
    // 每次操作重连且检查代际，不让取消/前次退出信号提交下一次输出。
    s.process.disconnect(this);
    connect(&s.process, &QProcess::readyReadStandardOutput, this, [this, operation] {
        if (operation == state_->generation) drain();
    });
    connect(&s.process, &QProcess::readyReadStandardError, this, [this, operation] {
        if (operation == state_->generation) drain();
    });
    connect(&s.process, &QProcess::started, this, [this, operation] {
        if (operation != state_->generation) return;
#ifdef Q_OS_WIN
        // 只打开QProcess本次创建且仍活动的PID；JOB_LIST已从创建时附加。
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(processId()));
        BOOL attached = FALSE;
        const bool verified = process && IsProcessInJob(process, state_->job->handle, &attached) && attached;
        if (process) CloseHandle(process);
        if (!verified) { stop(QStringLiteral("CAD进程Job约束验证失败，已取消。")); return; }
        state_->confined = true;
#endif
    });
    connect(&s.process, &QProcess::errorOccurred, this, [this, operation](QProcess::ProcessError error) {
        if (operation != state_->generation) return;
        if (error == QProcess::FailedToStart)
            complete({}, QStringLiteral("无法启动受信CAD转换器：%1").arg(state_->process.errorString()));
    });
    connect(&s.process, &QProcess::finished, this, [this, operation](int code, QProcess::ExitStatus status) {
        if (operation == state_->generation) finish(code, status == QProcess::NormalExit);
    });
    s.process.setWorkingDirectory(s.directory->path());
    s.process.setProcessChannelMode(QProcess::SeparateChannels);
    const QPointer<CadImportProcess> owner(this);
    s.process.start(config.python, {QStringLiteral("-I"), config.converter,
        QStringLiteral("--input"), source.absoluteFilePath(), QStringLiteral("--output"), s.output,
        QStringLiteral("--modules"), config.modules});
    if (!owner) return true; // 启动失败回调允许调用者销毁拥有者。
    if (s.busy && s.generation == operation) {
        s.deadline.start(s.config.timeoutMs);
        s.outputWatch.start();
    }
    return true;
}

void CadImportProcess::drain()
{
    auto &s = *state_;
    // 逐块实际消费两个管道；只记累计字节数，不保存或输出模型/内核日志。
    for (auto channel : {QProcess::StandardOutput, QProcess::StandardError}) {
        s.process.setReadChannel(channel);
        while (s.process.bytesAvailable() > 0) {
            const QByteArray bytes = s.process.read(4096);
            if (bytes.isEmpty()) break;
            s.logs += bytes.size();
            if (s.logs > maximumLogs && !s.stopping) {
                stop(QStringLiteral("CAD转换日志超过128 KiB，已取消。"));
                return;
            }
        }
    }
}

void CadImportProcess::stop(const QString &error)
{
    auto &s = *state_;
    if (!s.busy) return;
    const quint64 operation = s.generation;
    s.stopping = true; s.stopError = error; s.error = error;
    s.deadline.stop(); s.outputWatch.stop();
#ifdef Q_OS_WIN
    if (s.job) TerminateJobObject(s.job->handle, 1);
#endif
    if (s.process.state() != QProcess::NotRunning) {
        const QPointer<CadImportProcess> owner(this);
        s.process.kill();
        s.process.waitForFinished(3000);
        if (!owner) return;
    }
    if (s.generation != operation) return;
    if (s.process.state() == QProcess::NotRunning && s.busy)
        complete({}, error);
    else if (s.busy)
        s.error = error + QStringLiteral(" 进程退出尚未确认，临时输出已保留；禁止再次导入。");
}

bool CadImportProcess::cancel()
{
    if (!state_->busy) {
        // 转换结束与业务提交之间的排队回调也可被取消。
        ++state_->generation;
        return true;
    }
    const QPointer<CadImportProcess> owner(this);
    stop(QStringLiteral("CAD导入已取消；原设备和资料保留。"));
    return !owner || !state_->busy;
}

void CadImportProcess::finish(int exitCode, bool normalExit)
{
    auto &s = *state_;
    if (!s.busy) return;
    const quint64 operation = s.generation;
    const QPointer<CadImportProcess> owner(this);
    drain();
    if (!owner) return;
    if (!s.busy || s.generation != operation) return;
    if (s.stopping) { complete({}, s.stopError); return; }
    if (!normalExit || exitCode != 0) {
        complete({}, QStringLiteral("CAD转换失败（退出码%1）；原设备和资料保留。").arg(exitCode)); return;
    }
    QFile file(s.output);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > maximumOutput) {
        file.close();
        complete({}, QStringLiteral("CAD转换结果缺失、为空或超过64 MiB；原设备保留。")); return;
    }
    QByteArray bytes = file.read(maximumOutput + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximumOutput) {
        file.close();
        complete({}, QStringLiteral("CAD转换结果读取失败或超限；原设备保留。")); return;
    }
    file.close();
    complete(std::move(bytes), {});
}

void CadImportProcess::complete(QByteArray bytes, const QString &error)
{
    auto &s = *state_;
    if (!s.busy || s.process.state() != QProcess::NotRunning) return;
    s.deadline.stop(); s.outputWatch.stop();
    s.busy = false; s.confined = false; s.error = error;
    ++s.generation;
    s.process.disconnect(this);
#ifdef Q_OS_WIN
    s.process.setCreateProcessArgumentsModifier({});
    s.job.reset();
#endif
    s.directory.reset();
    auto completion = std::move(s.completion);
    const quint64 operation = s.generation;
    // 脱离QProcess信号栈后通知调用者，允许回调销毁拥有者或开始下一次操作。
    QTimer::singleShot(0, this, [this, operation, completion = std::move(completion),
                               bytes = std::move(bytes), error]() mutable {
        if (state_->generation == operation && completion)
            completion(std::move(bytes), error);
    });
}
