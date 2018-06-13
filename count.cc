//
// Created by frank on 18-1-15.
//

//
// Created by frank on 18-1-3.
//

#include <unordered_map>
#include <memory>

#include <eva/Analyzer.h>

using namespace eva;

class Counter
{
public:
    void put(uint32_t len)
    {
        if (len > 1500) {
            printf("bad len\n");
            exit(1);
        }
        cnt_[len]++;
    }

    void print()
    {
        for (int i = 0; i < 2000; i++) {
            if (cnt_[i] > 0) {
                printf("%d: %u\n", i, cnt_[i]);
            }
        }
        printf("\n");
    }

private:
    uint32_t cnt_[2000] = {};
};

int main(int argc, char** argv)
{
    if (argc != 3) {
        printf("./run srcAddress interface/file");
        exit(1);
    }

    Logger::setLogLevel(Logger::WARN);

    const char* srcAddress = argv[1];
    const char* interface = argv[2];
    const char* file = interface;

    printf("%s %s\n", srcAddress, interface);

    char errbuf[PCAP_ERRBUF_SIZE];
    pcap* cap = pcap_open_offline(file, errbuf);
    if (cap == nullptr) {
        printf("%s\n", errbuf);
        exit(1);
    }

    int linkType = pcap_datalink(cap);
    if (linkType == PCAP_ERROR_NOT_ACTIVATED) {
        printf("%s\n", errbuf);
        exit(1);
    }

    struct pcap_pkthdr hdr;
    const uint8_t* data;

    Counter cnt;
    while ((data = pcap_next(cap, &hdr)) != nullptr) {

        eva::Unit u;
        bool ok = eva::unpack(&hdr, data, linkType, &u);
        if (!ok) {
            continue;
        }
        if (u.srcAddress.toIp() == srcAddress) {

            uint32_t expect = 1500 - 40 - u.optionLength;
            uint32_t len = u.dataLength;

            while (len > expect) {
                cnt.put(expect);
                len -= expect;
            }
            if (len > 0)
                cnt.put(len);
        }
    }
    cnt.print();
}
