/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstring>
#include "esp_log.h"
#include "cxx_include/esp_modem_dte.hpp"
#include "cxx_include/esp_modem_cmux.hpp"
#include "esp_modem_config.h"

using namespace esp_modem;

static const size_t dte_default_buffer_size = 1000;

DTE::DTE(const esp_modem_dte_config *config, std::unique_ptr<Terminal> terminal):
    buffer(config->dte_buffer_size),
    cmux_term(nullptr), primary_term(std::move(terminal)), secondary_term(primary_term),
    mode(modem_mode::UNDEF)
{
    set_cb();
}

DTE::DTE(std::unique_ptr<Terminal> terminal):
    buffer(dte_default_buffer_size),
    cmux_term(nullptr), primary_term(std::move(terminal)), secondary_term(primary_term),
    mode(modem_mode::UNDEF)
{
    set_cb();
}

DTE::DTE(const esp_modem_dte_config *config, std::unique_ptr<Terminal> t, std::unique_ptr<Terminal> s):
    buffer(config->dte_buffer_size),
    cmux_term(nullptr), primary_term(std::move(t)), secondary_term(std::move(s)),
    mode(modem_mode::UNDEF)
{
    set_cb();
}

DTE::DTE(std::unique_ptr<Terminal> t, std::unique_ptr<Terminal> s):
    buffer(dte_default_buffer_size),
    cmux_term(nullptr), primary_term(std::move(t)), secondary_term(std::move(s)),
    mode(modem_mode::UNDEF)
{
    set_cb();
}

void DTE::set_cb()
{
    primary_term->set_read_cb([this](uint8_t *data, size_t len) {
        Scoped<Lock> l(got_line_lock);
        if (line_cb == nullptr) {
            return false;
        }
        if (data) {
            if (extra.consumed != 0) {
                grow(extra.consumed + len);
                ::memcpy(&extra.data[0] + extra.consumed, data, len);
                data = &extra.data[0];
            }
            if (memchr(data + extra.consumed, '\n', len)) {
                result = line_cb(data, extra.consumed + len);
                if (result == command_result::OK || result == command_result::FAIL) {
                    signal.set(GOT_LINE);
                    return true;
                }
            }
            if (extra.consumed == 0) {
                grow(len);
                ::memcpy(&extra.data[0], data, len);
            }
            extra.consumed += len;
            return false;
        }
        data = buffer.get();
        int remaining = ((int) buffer.size) - ((int) buffer.consumed);
        ESP_LOGE("dte", "size=%d len=%d remaining=%d consumed=%d", buffer.size, len, remaining, buffer.consumed);
        if (remaining > 0) {
            len = primary_term->read(data + buffer.consumed, buffer.size - buffer.consumed);
        } else {
            if (extra.consumed == 0) {
                grow(buffer.size + len);
                ::memcpy(&extra.data[0], buffer.get(), buffer.size);
                extra.consumed = buffer.size;
            } else {
                grow(extra.consumed + len);
            }
            data = &extra.data[0];
            len = primary_term->read(data + extra.consumed, extra.data.size() - extra.consumed);
            if (memchr(data + extra.consumed, '\n', len)) {
                result = line_cb(data, extra.consumed + len);
                if (result == command_result::OK || result == command_result::FAIL) {
                    signal.set(GOT_LINE);
                    return true;
                }
            }
            extra.consumed += len;
            return false;
        }
        if (memchr(data + buffer.consumed, '\n', len)) {
            result = line_cb(data, buffer.consumed + len);
            if (result == command_result::OK || result == command_result::FAIL) {
                signal.set(GOT_LINE);
                return true;
            }
        }
        buffer.consumed += len;
        return false;
//        std::weak_ptr<SignalGroup> weak_signal = shared_signal;
//
//        got_line_cb line_cb = got_line;
//        return on_term_recv(data, len);
    });
}

command_result DTE::command(const std::string &command, got_line_cb got_line, uint32_t time_ms, const char separator)
{
    Scoped<Lock> l1(internal_lock);
    {
        Scoped<Lock> l2(got_line_lock);
        line_cb = got_line;
    }
    primary_term->write((uint8_t *)command.c_str(), command.length());
    auto got_lf = signal.wait(GOT_LINE, time_ms);
    if (got_lf && result == command_result::TIMEOUT) {
        ESP_MODEM_THROW_IF_ERROR(ESP_ERR_INVALID_STATE);
    }
    {
        Scoped<Lock> l2(got_line_lock);
        line_cb = nullptr;
        buffer.consumed = 0;
        if (extra.consumed) {
            printf("\n%.*s\n", extra.data.size(), &extra.data[0]);
        }
        extra.consumed = 0;
    }

    return result;

    signal.set(GOT_LINE);

    result = command_result::TIMEOUT;
    signal.clear(GOT_LINE);
    primary_term->set_read_cb([this, got_line, separator](uint8_t *data, size_t len) {
        if (signal.is_any(GOT_LINE)) {
            return false;
        }
//        if (data)
//        {
//            if (memchr(data, separator, len)) {
//                result = got_line(data, len);
//                if (result == command_result::OK || result == command_result::FAIL) {
//                    signal.set(GOT_LINE);
//                    primary_term->set_read_cb(nullptr);
//                    return true;
//                }
//            }
//            return false;
//        }
        if (!data) {
            data = buffer.get();
            int remaining = ((int)buffer.size) - ((int)buffer.consumed);
            ESP_LOGE("dte", "size=%d len=%d remaining=%d consumed=%d", buffer.size, len, remaining, buffer.consumed);
            if (remaining > 0) {
                len = primary_term->read(data + buffer.consumed, buffer.size - buffer.consumed);
            } else {
                if (extra.consumed == 0) {
                    grow(buffer.size + len);
                    ::memcpy(&extra.data[0], buffer.get(), buffer.size);
                    extra.consumed = buffer.size;
                } else {
                    grow(extra.consumed + len);
                }
                data = &extra.data[0];
                len = primary_term->read(data + extra.consumed, extra.data.size() - extra.consumed);
                if (memchr(data + extra.consumed, separator, len)) {
                    result = got_line(data, extra.consumed + len);
                    if (result == command_result::OK || result == command_result::FAIL) {
                        signal.set(GOT_LINE);
                        primary_term->set_read_cb(nullptr);
                        return true;
                    }
                }
                extra.consumed += len;
                return false;
            }
        } else {
            if (extra.consumed != 0) {
                grow(extra.consumed + len);
                ::memcpy(&extra.data[0] + extra.consumed, data, len);
                data = &extra.data[0];
            }
            if (memchr(data + extra.consumed, separator, len)) {
                result = got_line(data, extra.consumed + len);
                if (result == command_result::OK || result == command_result::FAIL) {
                    signal.set(GOT_LINE);
                    primary_term->set_read_cb(nullptr);
                    return true;
                }
            }
            if (extra.consumed == 0) {
                grow(len);
                ::memcpy(&extra.data[0], data, len);
            }
            extra.consumed += len;
            return false;
        }
        if (memchr(data + buffer.consumed, separator, len)) {
            result = got_line(data, buffer.consumed + len);
            if (result == command_result::OK || result == command_result::FAIL) {
                signal.set(GOT_LINE);
                primary_term->set_read_cb(nullptr);
                return true;
            }
        }
        buffer.consumed += len;
        return false;
    });
    primary_term->write((uint8_t *)command.c_str(), command.length());
    auto got_lf2 = signal.wait(GOT_LINE, time_ms);
    if (got_lf2 && result == command_result::TIMEOUT) {
        ESP_MODEM_THROW_IF_ERROR(ESP_ERR_INVALID_STATE);
    }
    signal.set(GOT_LINE);
    primary_term->set_read_cb(nullptr);
    if (extra.consumed) {
        printf("\n%.*s\n", extra.data.size(), &extra.data[0]);
    }
    buffer.consumed = 0;
    extra.consumed = 0;
    return result;
}

command_result DTE::command(const std::string &cmd, got_line_cb got_line, uint32_t time_ms)
{
    return command(cmd, got_line, time_ms, '\n');
}

bool DTE::exit_cmux()
{
    if (!cmux_term->deinit()) {
        return false;
    }
    auto ejected = cmux_term->detach();
    // return the ejected terminal and buffer back to this DTE
    primary_term = std::move(ejected.first);
    buffer = std::move(ejected.second);
    secondary_term = primary_term;
    set_cb();
    return true;
}

bool DTE::setup_cmux()
{
    cmux_term = std::make_shared<CMux>(primary_term, std::move(buffer));
    if (cmux_term == nullptr) {
        return false;
    }
    if (!cmux_term->init()) {
        return false;
    }
    primary_term = std::make_unique<CMuxInstance>(cmux_term, 0);
    if (primary_term == nullptr) {
        return false;
    }
    secondary_term = std::make_unique<CMuxInstance>(cmux_term, 1);
    set_cb();
    return true;
}

bool DTE::set_mode(modem_mode m)
{
    // transitions (COMMAND|UNDEF) -> CMUX
    if (m == modem_mode::CMUX_MODE) {
        if (mode == modem_mode::UNDEF || mode == modem_mode::COMMAND_MODE) {
            if (setup_cmux()) {
                mode = m;
                return true;
            }
            mode = modem_mode::UNDEF;
            return false;
        }
    }
    // transitions (COMMAND|DUAL|CMUX|UNDEF) -> DATA
    if (m == modem_mode::DATA_MODE) {
        if (mode == modem_mode::CMUX_MODE || mode == modem_mode::CMUX_MANUAL_MODE || mode == modem_mode::DUAL_MODE) {
            // mode stays the same, but need to swap terminals (as command has been switched)
            secondary_term.swap(primary_term);
        } else {
            mode = m;
        }
        return true;
    }
    // transitions (DATA|DUAL|CMUX|UNDEF) -> COMMAND
    if (m == modem_mode::COMMAND_MODE) {
        if (mode == modem_mode::CMUX_MODE) {
            if (exit_cmux()) {
                mode = m;
                return true;
            }
            mode = modem_mode::UNDEF;
            return false;
        } if (mode == modem_mode::CMUX_MANUAL_MODE || mode == modem_mode::DUAL_MODE) {
            return true;
        } else {
            mode = m;
            return true;
        }
    }
    // manual CMUX transitions: Enter CMUX
    if (m == modem_mode::CMUX_MANUAL_MODE) {
        if (setup_cmux()) {
            mode = m;
            return true;
        }
        mode = modem_mode::UNDEF;
        return false;
    }
    // manual CMUX transitions: Exit CMUX
    if (m == modem_mode::CMUX_MANUAL_EXIT && mode == modem_mode::CMUX_MANUAL_MODE) {
        if (exit_cmux()) {
            mode = modem_mode::COMMAND_MODE;
            return true;
        }
        mode = modem_mode::UNDEF;
        return false;
    }
    // manual CMUX transitions: Swap terminals
    if (m == modem_mode::CMUX_MANUAL_SWAP && mode == modem_mode::CMUX_MANUAL_MODE) {
        secondary_term.swap(primary_term);
        return true;
    }
    mode = modem_mode::UNDEF;
    return false;
}

void DTE::set_read_cb(std::function<bool(uint8_t *, size_t)> f)
{
    if (f == nullptr) {
        set_cb();
        return;
    }
    on_data = std::move(f);
    secondary_term->set_read_cb([this](uint8_t *data, size_t len) {
        if (!data) { // if no data available from terminal callback -> need to explicitly read some
            data = buffer.get();
            len = secondary_term->read(buffer.get(), buffer.size);
        }
        if (on_data) {
            return on_data(data, len);
        }
        return false;
    });
}

void DTE::set_error_cb(std::function<void(terminal_error err)> f)
{
    secondary_term->set_error_cb(f);
    primary_term->set_error_cb(f);
}

int DTE::read(uint8_t **d, size_t len)
{
    auto data_to_read = std::min(len, buffer.size);
    auto data = buffer.get();
    auto actual_len = secondary_term->read(data, data_to_read);
    *d = data;
    return actual_len;
}

int DTE::write(uint8_t *data, size_t len)
{
    return secondary_term->write(data, len);
}

int DTE::write(DTE_Command command)
{
    return primary_term->write(command.data, command.len);
}

void DTE::on_read(got_line_cb on_read_cb)
{
    if (on_read_cb == nullptr) {
        primary_term->set_read_cb(nullptr);
        internal_lock.unlock();
        return;
    }
    internal_lock.lock();
    primary_term->set_read_cb([this, on_read_cb](uint8_t *data, size_t len) {
        if (!data) {
            data = buffer.get();
            len = primary_term->read(data, buffer.size);
        }
        auto res = on_read_cb(data, len);
        if (res == command_result::OK || res == command_result::FAIL) {
            primary_term->set_read_cb(nullptr);
            internal_lock.unlock();
            return true;
        }
        return false;
    });
}

/**
 * Implemented here to keep all headers C++11 compliant
 */
unique_buffer::unique_buffer(size_t size):
    data(std::make_unique<uint8_t[]>(size)), size(size), consumed(0) {}

void DTE::extra::grow(size_t need_size)
{
    if (need_size == 0) {
        delete buffer;
        buffer = nullptr;
    } else if (buffer == nullptr) {
        buffer = new std::vector<uint8_t>(need_size);
    } else {
        buffer->resize(need_size);
    }
}
