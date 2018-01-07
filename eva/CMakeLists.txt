add_library(eva STATIC
        Unit.h Unit.cc
        checksum.h checksum.cc
        util.h Exception.h
        TcpFlow.cc TcpFlow.h
        hash.cc hash.h
        RateSample.h
        Analyzer.cc Analyzer.h
        Filter.h Roundtrip.h)
target_link_libraries(eva muduo_net)