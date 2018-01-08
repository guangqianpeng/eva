//
// Created by frank on 18-1-8.
//

#include <eva/util.h>


using namespace eva;

class Client
{
public:
    Client(EventLoop* loop, const InetAddress& addr, int n)
    {
        for (int i = 0; i < n; i++) {
            char name[32];
            snprintf(name, sizeof(name), "client[%d]", i);
            auto client = new TcpClient(loop, addr, name);
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
    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp time)
    {
        auto msg(buf->retrieveAllAsString());
        LOG_DEBUG << conn->name() << " discards " << msg.size()
                  << " bytes received at ";
    }

private:
    typedef std::unique_ptr<TcpClient> TcpClientPtr;
    typedef std::vector<TcpClientPtr> TcpClientList;
    TcpClientList clients_;
};

int main(int argc, char** argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: client <address> <port> <num>");
        exit(1);
    }

    auto ip = argv[1];
    auto port = static_cast<uint16_t>(atoi(argv[2]));
    int n = atoi(argv[3]);

    EventLoop loop;
    InetAddress addr(ip, port);
    Client client(&loop, addr, n);
    client.connect();
    loop.loop();
}