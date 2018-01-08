#include <eva/Analyzer.h>
#include <eva/util.h>

int main()
{
//    eva::Logger::setLogLevel(eva::Logger::DEBUG);

    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *cap = pcap_open_offline("flow5.pcap", errbuf);
//    const char* srcAddress = "192.168.1.148";
//    const char* srcAddress = "192.168.0.100";
    const char* srcAddress = "192.168.0.131";

    if (cap == nullptr) {
        LOG_ERROR << errbuf;
        exit(1);
    }

    int linkType = pcap_datalink(cap);
    if (linkType == PCAP_ERROR_NOT_ACTIVATED) {
        LOG_ERROR << errbuf;
        exit(1);
    }

    int totalPackets = 0;
    int invalidPackets = 0;

    struct pcap_pkthdr hdr;
    const uint8_t* data;

    eva::Analyzer* analyzer = nullptr;

    while ((data = pcap_next(cap, &hdr)) != nullptr) {

        totalPackets++;

        eva::Unit unit;
        bool ok = eva::unpack(&hdr, data, linkType, &unit, true);
        if (!ok) {
            invalidPackets++;
            continue;
        }

        if (unit.srcAddress.toIp() == srcAddress)
        {
            eva::DataUnit dataUnit(&unit);
            if (analyzer == nullptr) {
                if (unit.isSYN() || unit.dataLength > 0) {
                    analyzer = new eva::Analyzer(dataUnit);
                    analyzer->onDataUnit(dataUnit);
                }
            }
            else if (unit.dataLength > 0 || unit.isSYN()) {
                analyzer->onDataUnit(dataUnit);
            }


            if (unit.isFIN() || unit.isRST()) {
                break;
            }
        }
        else {
            eva::AckUnit ackUnit(&unit);
            if (analyzer == nullptr) {
                if (unit.isSYN()) {
                    analyzer = new eva::Analyzer(ackUnit);
                    analyzer->onAckUnit(ackUnit);
                }
            }
            else if (!unit.isRST()) {
                analyzer->onAckUnit(ackUnit);
            }
            else {
                break;
            }
        }
    }

    if (analyzer != nullptr)
        delete analyzer;
    pcap_close(cap);
}