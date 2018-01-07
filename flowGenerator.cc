//
// Created by frank on 18-1-7.
//

#include <eva/util.h>

using namespace eva;

class BulkServer: noncopyable
{
public:
    BulkServer(EventLoop* loop, const InetAddress& addr):
            loop_(loop),
            addr_(addr),
            server_(loop, addr, "bulk data transfer")
    {
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
        LOG_INFO << "start bulk data transfer server "
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
            LOG_INFO << conn->name() << "up";
        }
        else {
            LOG_INFO << conn->name() << "down";
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
        conn->send(message_);
    }

private:
    EventLoop* loop_;
    InetAddress addr_;
    TcpServer server_;
    std::string congestionControl_;
    std::string message_;
};

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: server <address> <port> <threads>\n");
        exit(1);
    }

    const char* ip = argv[1];
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    const char* congestionControl = argv[3];

    EventLoop loop;

    InetAddress addr(ip, port);
    BulkServer server(&loop, addr);
    server.setCongestionControl(congestionControl);
    server.start();

    loop.loop();
}