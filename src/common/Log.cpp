/// @file Log.cpp
/// Log class implementation

#include "Log.h"
#include "RuntimeConfig.h"

#include <memory>
#include <map>

using namespace cali;
using namespace std;


struct LogImpl
{
    // --- data

    static unique_ptr<LogImpl>    s_instance;
    static const char*            s_prefix;

    static const ConfigSet::Entry s_configdata[];

    enum class Stream { StdOut, StdErr, None, File };

    ConfigSet     m_config;

    Stream        m_stream;
    std::ofstream m_ofstream;
    unsigned      m_verbosity;

    // --- helpers

    void init_stream() {
        string name = m_config.get("logfile").to_string();

        const map<string, Stream> strmap { 
            { "none",   Stream::None   },
            { "stdout", Stream::StdOut },
            { "stderr", Stream::StdErr } };

        auto it = strmap.find(name);

        if (it == strmap.end()) {
            m_stream = Stream::File;

            m_ofstream.open(name);

            if (!m_ofstream && m_verbosity > 0)
                cerr << s_prefix << "Could not open log file " << name << endl;
        } else
            m_stream = it->second;
    }

    // --- interface

    LogImpl() 
        : m_config { RuntimeConfig::init("log", s_configdata) } 
    {
        m_verbosity = m_config.get("verbosity").to_uint();
        init_stream();
    }

    ostream& get_stream() {
        switch (m_stream) {
        case Stream::StdOut:
            return std::cout;
        case Stream::StdErr:
            return std::cerr;
        default:
            return m_ofstream;
        }
    }

    static LogImpl* instance() {
        if (!s_instance)
            s_instance.reset(new LogImpl);

        return s_instance.get();
    }
};

unique_ptr<LogImpl>    LogImpl::s_instance { nullptr } ;
const char*            LogImpl::s_prefix   { "== CALIPER: " };

const ConfigSet::Entry LogImpl::s_configdata[] = {
    // key, type, value, short description, long description
    { "verbosity", CALI_TYPE_UINT,   "1",
      "Verbosity level",
      "Verbosity level.\n"
      "  0: no output\n"
      "  1: basic informational runtime output\n"
      "  2: debug output" 
    },
    { "logfile",   CALI_TYPE_STRING, "stderr",
      "Log file name",
      "Log file name or output stream. Either one of\n"
      "   stdout: Standard output stream,\n"
      "   stderr: Standard error stream,\n"
      "   none:   No output,\n"
      " or a log file name."
    },
    ConfigSet::Terminator 
};


//
// --- Log public interface
//

ostream& 
Log::get_stream()
{
    return (LogImpl::instance()->get_stream() << LogImpl::s_prefix);
}

unsigned 
Log::verbosity()
{
    return LogImpl::instance()->m_verbosity;
}