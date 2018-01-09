//
// Created by frank on 18-1-7.
//

#include <random>

#include <eva/util.h>

using namespace eva;

class BulkServer: noncopyable
{
public:
    BulkServer(EventLoop* loop, const InetAddress& addr, int64_t kiB):
            loop_(loop),
            addr_(addr),
            server_(loop, addr, "Bulk server"),
            kiB_(kiB)
    {
        server_.setConnectionCallback(std::bind(
                &BulkServer::onConnection, this, _1));
        server_.setMessageCallback(std::bind(
                &BulkServer::onMessage, this, _1, _2, _3));
        server_.setWriteCompleteCallback(std::bind(
                &BulkServer::onWriteComplete, this, _1));

        std::string line;
        for (int i = 33; i < 127; ++i)
        {
            line.push_back(char(i));
        }
        line += line;

        for (size_t i = 0; i < 127-33; ++i)
        {
            message_ += line.substr(i, 72) + '\n';
        }
    }

    void start()
    {
        LOG_INFO << "BulkServer(" << congestionControl_ << ") " << kiB_ << "kiB "
                 << addr_.toIpPort();
        server_.start();
    }

    void setCongestionControl(const std::string &name)
    {
        congestionControl_ = name;
    }

private:
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected()) {
            LOG_INFO << conn->name() << " [up]";
            conn->setCongestionControl(congestionControl_.c_str());
            conn->setContext(kiB_ * 1024);
            onWriteComplete(conn);
        }
        else {
            LOG_INFO << conn->name() << " [down]";
        }
    }

    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time)
    {
        auto msg(buf->retrieveAllAsString());
        LOG_INFO << conn->name() << " discards " << msg.size()
                 << " bytes received at ";
    }

    void onWriteComplete(const TcpConnectionPtr& conn)
    {
        auto restBytes = boost::any_cast<int64_t>(conn->getContext());
        auto length = static_cast<int64_t>(message_.length());
        if (restBytes < 0 || restBytes >= length) {
            conn->send(message_);
            restBytes -= length;
        }
        else if (restBytes > 0) {
            conn->send(message_.c_str(),
                       static_cast<int>(restBytes));
            restBytes = 0;
        }
        else {
            loop_->runAfter(1, [=](){
                conn->shutdown();
            });
        }
        conn->setContext(restBytes);
    }

private:
    EventLoop* loop_;
    InetAddress addr_;
    TcpServer server_;
    std::string congestionControl_;
    std::string message_;
    const int64_t kiB_;
};

const double kMaxInterval = 1; // second
const int kMaxMessageLen = 14600;

class InteractServer: noncopyable
{
public:
    InteractServer(EventLoop* loop, const InetAddress& addr):
            loop_(loop),
            addr_(addr),
            server_(loop, addr, "Interact server")
    {
        server_.setConnectionCallback(std::bind(
                &InteractServer::onConnection, this, _1));
        server_.setMessageCallback(std::bind(
                &InteractServer::onMessage, this, _1, _2, _3));

        auto seed = static_cast<unsigned long>(time(nullptr));
        std::default_random_engine generator(seed);
        std::uniform_int_distribution<int> messageLen(1, kMaxMessageLen);
        // 1ms ~ 1s
        std::uniform_real_distribution<double> interval(0.001, kMaxInterval);

        generateMessageLen = std::bind(messageLen, generator);
        generateInterval = std::bind(interval, generator);

        std::fill(message_, message_ + kMaxMessageLen, 'x');
    }

    void start()
    {
        LOG_INFO << "Interact server(" << congestionControl_ << ") "
                 << addr_.toIpPort();
        server_.start();
    }

    void setCongestionControl(const std::string &name)
    {
        congestionControl_ = name;
    }

private:
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected()) {
            LOG_INFO << conn->name() << " [up]";
            conn->setCongestionControl(congestionControl_.c_str());
            sendMessage(conn);
        }
        else {
            LOG_INFO << conn->name() << " [down]";
        }
    }

    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time)
    {
        auto msg(buf->retrieveAllAsString());
        LOG_INFO << conn->name() << " discards " << msg.size()
                 << " bytes received at ";
    }

    void sendMessage(const TcpConnectionPtr& conn)
    {
        int len = generateMessageLen();
        conn->send(message_, len);

        double interval = generateInterval();
        loop_->runAfter(interval, [this, conn](){
           sendMessage(conn);
        });
    }

private:
    EventLoop* loop_;
    InetAddress addr_;
    TcpServer server_;
    std::string congestionControl_;

    char message_[kMaxMessageLen];

    std::function<int()>    generateMessageLen;
    std::function<double()> generateInterval;
};

int main(int argc, char** argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: server <-b/i/f> <address> <port> <congestion control> [KiB]\n");
        exit(1);
    }

    char type = argv[1][1];
    auto ip = argv[2];
    auto port = static_cast<uint16_t>(atoi(argv[3]));
    auto congestionControl = argv[4];
    auto mbytes = argc > 5 ? atoi(argv[5]) : -1;

    EventLoop loop;

    InetAddress addr(ip, port);

    if (type == 'i') {
        if (argc > 5) {
            LOG_WARN << "arg 5 not used";
        }
        InteractServer server(&loop, addr);
        server.setCongestionControl(congestionControl);
        server.start();
        loop.loop();
    }
    else {
        BulkServer server(&loop, addr, mbytes);
        server.setCongestionControl(congestionControl);
        server.start();
        loop.loop();
    }
}