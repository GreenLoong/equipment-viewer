#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>

/** 拥有独立CAD转换进程；只返回有界数据，不写数据库，必须在所属Qt线程使用。 */
class CadImportProcess : public QObject {
public:
    /** 受信运行路径；必须为绝对路径，不能从模型目录或PATH发现Python及模块。 */
    struct Config {
        QString python;
        QString modules;
        QString converter;
        int timeoutMs = 120000;
        /** 退出确认总预算，最多3000毫秒；测试可缩短，但不能放宽生产上限。 */
        int exitConfirmationMs = 3000;
        /** 仅用于受控故障测试：返回false制造本对象Job查询失败，不提供Job/PID。 */
        std::function<bool()> allowJobQuery;
        /** 仅用于受控故障测试：返回false制造本对象Job终止失败，不提供Job/PID。 */
        std::function<bool()> allowJobTermination;
        /** 从显式环境变量或应用旁cad-runtime读取运行路径。 */
        static Config runtime();
    };
    /** 回调在所属线程执行；失败bytes为空，析构时不调用回调。 */
    using Completion = std::function<void(QByteArray bytes, QString error)>;
    /** 使用打包默认路径或显式环境变量；父对象销毁会取消转换。 */
    explicit CadImportProcess(QObject *parent = nullptr);
    /** 注入受信路径与测试期限，不执行模型目录中的脚本。 */
    explicit CadImportProcess(const Config &config, QObject *parent = nullptr);
    /** 先清回调再取消；未确认退出时保留临时目录供诊断。 */
    ~CadImportProcess() override;
    /** 启动STEP/STP转换；重入/路径/输入错误返回false，不调用completion。 */
    bool start(const QString &input, Completion completion);
    /** 取消并最多等待3秒确认退出；返回false时仍保持busy并保留临时目录。 */
    bool cancel();
    /** 转换或终止确认仍在进行时为true。 */
    bool isBusy() const;
    /** 返回本对象创建的活动进程ID；不接受外部进程ID。 */
    qint64 processId() const;
    /** Windows创建时Job约束是否已实际验证；其他平台为false。 */
    bool isConfined() const;
    /** 返回最近启动或取消错误，不包含输入内容。 */
    QString lastError() const;

private:
    struct State;
    std::unique_ptr<State> state_;
    void drain();
    void stop(const QString &error);
    void finish(int exitCode, bool normalExit);
    void complete(QByteArray bytes, const QString &error);
    bool confirmTreeEmpty(int budgetMs);
    bool terminateJob();
    void retainUnconfirmed(const QString &error);
};
