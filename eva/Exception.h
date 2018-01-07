//
// Created by frank on 18-1-2.
//

#ifndef EVA_EXCEPTION_H
#define EVA_EXCEPTION_H

#include <exception>

class Exception: public std::exception
{
public:
    explicit Exception(const char* msg):
            msg_(msg)
    {}

    const char* what() const noexcept
    {
        return msg_;
    }

private:
    const char* msg_;
};


#endif //EVA_EXCEPTION_H
