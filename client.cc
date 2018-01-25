//
// Created by frank on 18-1-8.
//

#include <eva/util.h>


using namespace eva;

class Client
{
public:
    Client(EventLoop* loop, const InetAddress& addr, int n, int64_t kiB):
            loop_(loop),
            kiB_(kiB)
    {
        for (int i = 0; i < n; i++) {
            char name[32];
            snprintf(name, sizeof(name), "client[%d]", i);
            auto client = new TcpClient(loop, addr, name);
            client->setConnectionCallback(std::bind(
                    &Client::onConnection, this, _1));
            client->setMessageCallback(std::bind(
                    &Client::onMessage, this, _1, _2, _3));
            clients_.emplace_back(client);
        }
    }

    void connect()
    {
        for(auto& c: clients_) {
            c->connect();
        }
    }

private:
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected()) {
            if (kiB_ > 0) {
                int64_t add = kiB_ * 1024 / 100; // bytes/100ms
                setConnReadable(conn, add);
                loop_->runEvery(0.01, [=]() {
                    int64_t readable = getConnReadable(conn) + add;
                    setConnReadable(conn, readable);
                    if (readable > 0)
                        conn->startRead();
                });
            }
        }
        else {
            conn->shutdown();
        }
    }

    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time)
    {
        auto msg(buf->retrieveAllAsString());
        printf("%s", msg.c_str());

        if (kiB_ > 0) {
            int64_t readable = getConnReadable(conn);
            readable -= static_cast<int64_t>(msg.size());
            if (readable <= 0)
                conn->stopRead();
            conn->setContext(readable);
        }
    }

    int64_t getConnReadable(const TcpConnectionPtr& conn)
    {
        return boost::any_cast<int64_t>(conn->getContext());
    }

    void setConnReadable(const TcpConnectionPtr& conn, int64_t readable)
    {
        conn->setContext(readable);
    }

private:
    typedef std::unique_ptr<TcpClient> TcpClientPtr;
    typedef std::vector<TcpClientPtr> TcpClientList;
    EventLoop *loop_;
    TcpClientList clients_;
    const int64_t kiB_;
};

int main(int argc, char** argv)
{
    if (argc != 5) {
        fprintf(stderr, "Usage: client <address> <port> <num> <kiB/s>");
        exit(1);
    }

    auto ip = argv[1];
    auto port = static_cast<uint16_t>(atoi(argv[2]));
    int n = atoi(argv[3]);
    int64_t kiB = atoi(argv[4]);

    EventLoop loop;
    InetAddress addr(ip, port);
    Client client(&loop, addr, n, kiB);
    client.connect();
    loop.loop();
}