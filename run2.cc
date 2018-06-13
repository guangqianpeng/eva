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

    bool analyzed = false;
    int n_packet = 0;
    int n_flow = 0;
    while ((data = pcap_next(cap, &hdr)) != nullptr) {

        n_packet++;

        eva::Unit unit;
        bool ok = eva::unpack(&hdr, data, linkType, &unit);
        if (!ok) {
            continue;
        }

        auto it = flowMap.find(unit);

        // data unit
        if (unit.srcAddress.toIp() == srcAddress)
        {
            eva::DataUnit dataUnit(&unit);

            if (it == flowMap.end())
            {
                if (unit.isFIN() || unit.isRST()) {
                    continue;
                }
                if (unit.isSYN() || unit.dataLength > 0) {
                    auto analyzer = new Analyzer(dataUnit);
                    analyzer->onDataUnit(dataUnit);
                    flowMap.emplace(unit, analyzer);
                    n_flow++;
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
                analyzed = true;
            }
        }
            // ack unit
        else {
            eva::AckUnit ackUnit(&unit);
            if (it == flowMap.end()) {
                if (unit.isSYN()) {
                    auto analyzer = new Analyzer(ackUnit);
                    analyzer->onAckUnit(ackUnit);
                    flowMap.emplace(unit, analyzer);

                    n_flow++;
                }
            }
            else if (!unit.isRST()) {
                // unit.isFIN() should input, since sender can still send data
                it->second->onAckUnit(ackUnit);
            }
            else {
                delete it->second;
                flowMap.erase(it);
                analyzed = true;
            }
        }
    }
    for (auto& p: flowMap) {
        delete p.second;
        analyzed = true;
    }

    if (!analyzed) {
        printf("0 0 0 0 0 0 0 0    0 0 0 0 0 0 0 0    0 0 0 0 0 0 0 0 \n");
    }
    // printf("%d packets, %d connections\n", n_packet, n_flow);
}
