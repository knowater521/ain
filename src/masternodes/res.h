#ifndef DEFICHAIN_RES_H
#define DEFICHAIN_RES_H

#include <string>
#include <tinyformat.h>
#include <boost/optional.hpp>

struct Res
{
    bool ok;
    std::string msg;
    uint32_t code;

    Res() = delete;

    template<typename... Args>
    static Res Err(std::string const & err, const Args&... args) {
        return Res{false, tfm::format(err, args...), 0};
    }

    template<typename... Args>
    static Res ErrCode(uint32_t code, std::string const & err, const Args&... args) {
        return Res{false, tfm::format(err, args...), code};
    }

    template<typename... Args>
    static Res Ok(std::string const & msg, const Args&... args) {
        return Res{true, tfm::format(msg, args...), 0};
    }

    static Res Ok() {
        return Res{true, {}, 0};
    }

    std::string ToString() const {
        if (!ok && code != 0) {
            return tfm::format("ERROR %d: %s", code, msg);
        }
        if (!ok) {
            return tfm::format("ERROR: %s", msg);
        }
        return msg;
    }
};

template <typename T>
struct ResVal : public Res
{
    boost::optional<T> val{};

    ResVal() = delete;

    ResVal(Res const & errRes) : Res(errRes) {
        assert(!this->ok); // if value is not provided, then it's always an error
    }
    ResVal(T value, Res const & okRes) : Res(okRes), val(std::move(value)) {
        assert(this->ok); // if value if provided, then it's never an error
    }

    Res res() const {
        return *this;
    }

    template <typename F>
    T ValOrException(F&& _throw) const {
        if (!ok) {
            _throw(msg);
            throw std::logic_error{msg}; // shouldn't be reachable because of _throw
        }
        return *val;
    }

    T ValOrDefault(T default_) const {
        if (!ok) {
            return std::move(default_);
        }
        return *val;
    }
};

#endif //DEFICHAIN_RES_H
