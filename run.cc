//
// Created by frank on 18-1-3.
//

#include <unordered_map>
#include <memory>

#include <eva/Analyzer.h>

using namespace eva;

int main(int argc, char** argv)
{
    if (argc != 4) {
        printf("./run srcAddress dstAddress interface/file");
        exit(1);
    }

    
    const char* srcAddress = argv[1];
    const char* dstAddress = argv[2];
    const char* interface = argv[3];
    const char* file = interface;

    printf("%s %s %s\n", srcAddress, dstAddress, interface);


    char errbuf[PCAP_ERRBUF_SIZE];
    pcap* cap = pcap_open_live(interface, 65560, 1, 0, errbuf);
    if (cap == nullptr) {
        printf("%s\n", errbuf);
        cap = pcap_open_offline(file, errbuf);
        if (cap == nullptr) {
            printf("%s\n", errbuf);
            exit(1);
        }
    }

    int linkType = pcap_datalink(cap);
    if (linkType == PCAP_ERROR_NOT_ACTIVATED) {
        printf("%s\n", errbuf);
        exit(1);
    }

    struct pcap_pkthdr hdr;
    const uint8_t* data;
    std::unordered_map<
            Unit,
            Analyzer*> flowMap;

    while ((data = pcap_next(cap, &hdr)) != nullptr) {

        eva::Unit unit;
        bool ok = eva::unpack(&hdr, data, linkType, &unit);
        if (!ok) {
            continue;
        }

        auto it = flowMap.find(unit);

        // data unit
        if (unit.srcAddress.toIp() == srcAddress &&
            unit.dstAddress.toIp() == dstAddress)
        {
            eva::DataUnit dataUnit(&unit);

            if (it == flowMap.end())
            {
                if (unit.isSYN() || unit.dataLength > 0) {
                    auto analyzer = new Analyzer(dataUnit);
                    analyzer->onDataUnit(dataUnit);
                    flowMap.emplace(unit, analyzer);
                }
            }
            else if (unit.dataLength > 0 || unit.isSYN())
            {
                it->second->onDataUnit(dataUnit);
            }
            else if (unit.isFIN() || unit.isRST())
            {
                delete it->second;
                flowMap.erase(it);
            }
        }
            // ack unit
        else if (unit.dstAddress.toIp() == srcAddress &&
                 unit.srcAddress.toIp() == dstAddress)
        {
            eva::AckUnit ackUnit(&unit);
            if (it == flowMap.end()) {
                if (unit.isSYN()) {
                    auto analyzer = new Analyzer(ackUnit);
                    analyzer->onAckUnit(ackUnit);
                    flowMap.emplace(unit, analyzer);
                }
            }
            else if (!unit.isRST()) {
                // unit.isFIN() should input, since sender can still send data
                it->second->onAckUnit(ackUnit);
            }
            else {
                delete it->second;
                flowMap.erase(it);
            }
        }
    }
}
