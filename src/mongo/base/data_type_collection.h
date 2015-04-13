/*    Copyright 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include "mongo/config.h"

#include <cstring>
#include <iterator>
#include <type_traits>

#include "mongo/base/data_type.h"
#include "mongo/base/error_codes.h"
#include "mongo/base/status.h"
#include "mongo/base/status_with.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    template <typename T>
    struct Consume {
        using const_iterator = typename T::const_iterator;

        Consume() {}

        Consume(const T& t) :
            begin(t.begin()),
            end(t.end()),
            push(nullptr)
        {}

        Consume(const_iterator begin, const_iterator end) :
            begin(begin),
            end(end),
            push(nullptr)
        {}

        Consume(T* push) :
            push(push)
        {}

        const_iterator begin;
        const_iterator end;
        T* push;
    };

    template <typename NT, typename T>
    struct Count {
        using const_iterator = typename T::const_iterator;

        Count() {}

        Count(const T& t) :
            begin(t.begin()),
            end(t.end()),
            count(t.size()),
            push(nullptr)
        {}

        Count(const_iterator begin, const_iterator end, NT count) :
            begin(begin),
            end(end),
            count(count),
            push(nullptr)
        {}

        Count(T* p) :
            push(p)
        {
        }

        const_iterator begin;
        const_iterator end;
        NT count;
        T* push;
    };

    template <typename T>
    struct DataType::Handler<Consume<T>> {
        using ConsumeType = Consume<T>;
        using U = typename T::value_type;

        static Status load(ConsumeType* ct, const char* ptr, size_t length, size_t* advanced,
                           std::ptrdiff_t debug_offset)
        {
            if (advanced) {
                *advanced = 0;
            }

            while (length) {
                size_t local_advanced = 0;

                if (ct) {
                    U u(DataType::defaultConstruct<U>());

                    Status status = DataType::load(&u, ptr, length, &local_advanced, debug_offset);

                    if (! status.isOK()) {
                        return status;
                    }

                    std::back_inserter(*ct->push) = std::move(u);
                } else {
                    Status status =
                        DataType::load<U>(nullptr, ptr, length, &local_advanced, debug_offset);

                    if (! status.isOK()) {
                        return status;
                    }
                }

                if (advanced) {
                    *advanced += local_advanced;
                }

                length -= local_advanced;
                ptr += local_advanced;
            }

            return Status::OK();
        }

        static Status store(const ConsumeType& ct, char* ptr, size_t length, size_t* advanced,
                            std::ptrdiff_t debug_offset)
        {
            if (advanced) {
                *advanced = 0;
            }

            for (auto iter = ct.begin; iter != ct.end; ++iter) {
                size_t local_advanced = 0;

                Status status = DataType::store(*iter, ptr, length, &local_advanced, debug_offset);

                if (! status.isOK()) {
                    return status;
                }

                if (advanced) {
                    *advanced += local_advanced;
                }

                length -= local_advanced;
                ptr += local_advanced;
            }

            return Status::OK();
        }

        static ConsumeType defaultConstruct()
        {
            return ConsumeType();
        }
    };

    template <typename NT, typename T>
    struct DataType::Handler<Count<NT, T>> {
        using CountType = Count<NT, T>;
        using U = typename T::value_type;

        static Status load(CountType* ct, const char* ptr, size_t length, size_t* advanced,
                           std::ptrdiff_t debug_offset)
        {
            if (advanced) {
                *advanced = 0;
            }

            NT count(DataType::defaultConstruct<NT>());
            size_t local_advanced = 0;

            Status status = DataType::load(&count, ptr, length, &local_advanced, debug_offset);

            if (! status.isOK()) {
                return status;
            }

            if (ct) {
                ct->count = count;
            }

            if (advanced) {
                *advanced += local_advanced;
            }

            length -= local_advanced;
            ptr += local_advanced;
            debug_offset += local_advanced;

            size_t i = 1;
            for (; i <= count; i++) {
                if (ct) {
                    U u(DataType::defaultConstruct<U>());

                    Status status = DataType::load(&u, ptr, length, &local_advanced, debug_offset);

                    if (! status.isOK()) {
                        return status;
                    }

                    std::back_inserter(*ct->push) = std::move(u);
                } else {
                    Status status =
                        DataType::load<U>(nullptr, ptr, length, &local_advanced, debug_offset);

                    if (! status.isOK()) {
                        return status;
                    }
                }

                if (advanced) {
                    *advanced += local_advanced;
                }

                length -= local_advanced;
                ptr += local_advanced;
                debug_offset += local_advanced;
            }

            if (count + 1 != i) {
                mongoutils::str::stream ss;
                ss << "only (" << i << ") elements read out of (" << count << ") in buffer["
                   << length << "] at offset: " << debug_offset;
                return Status(ErrorCodes::Overflow, ss);
            }

            return Status::OK();
        }

        static Status store(const CountType& ct, char* ptr, size_t length, size_t* advanced,
                            std::ptrdiff_t debug_offset)
        {
            if (advanced) {
                *advanced = 0;
            }
            size_t local_advanced = 0;

            Status status = DataType::store(ct.count, ptr, length, &local_advanced, debug_offset);

            if (! status.isOK()) {
                return status;
            }

            if (advanced) {
                *advanced += local_advanced;
            }

            length -= local_advanced;
            ptr += local_advanced;
            debug_offset += local_advanced;

            size_t i = 0;
            for (auto iter = ct.begin; iter != ct.end && i <= ct.count; ++iter) {
                i++;
                Status status = DataType::store(*iter, ptr, length, &local_advanced, debug_offset);

                if (! status.isOK()) {
                    return status;
                }

                if (advanced) {
                    *advanced += local_advanced;
                }

                length -= local_advanced;
                ptr += local_advanced;
                debug_offset += local_advanced;
            }

            if (ct.count != i) {
                mongoutils::str::stream ss;
                ss << "only (" << i << ") elements written out of (" << ct.count << ") in buffer["
                   << length << "] at offset: " << debug_offset;
                return Status(ErrorCodes::Overflow, ss);
            }

            return Status::OK();
        }

        static CountType defaultConstruct()
        {
            return CountType();
        }
    };

} // namespace mongo
