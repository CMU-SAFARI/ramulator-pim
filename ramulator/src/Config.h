#ifndef __CONFIG_H
#define __CONFIG_H

#include <string>
#include <fstream>
#include <vector>
#include <map>
#include <iostream>
#include <cassert>

namespace ramulator
{

class Config {
public:
    enum class Format
    {
        PISA,
        ZSIM,
        PIN
    } format;
private:
    std::map<std::string, std::string> options;
    int stacks;
    int channels;
    int ranks;
    int subarrays;
    int cpu_frequency;
    int core_num = 0;
    int cacheline_size = 64;
    long expected_limit_insts = 0;
    bool disable_per_scheduling = false;
    bool split_trace = false;
    std::string org = "in_order";

    bool pim_mode_enable = false;
public:
    Config() {}
    Config(const std::string& fname);
    void parse(const std::string& fname);
    std::string operator [] (const std::string& name) const {
      if (options.find(name) != options.end()) {
        return (options.find(name))->second;
      } else {
        return "";
      }
    }

    void set_trace_format(std::string trace){
        if(trace.find("pisa") != std::string::npos)
            format = Format::PISA;
        else if(trace.find("zsim") != std::string::npos)
            format = Format::ZSIM;
        else
            format = Format::PIN;
    }

    void set_org(std::string _org){org = _org;}
    void parse_to_const(const std::string& name, const std::string& value);
    void set_disable_per_scheduling(bool status){disable_per_scheduling = status;}
    void set_split_trace(bool status){split_trace = status;}
    bool get_disable_per_scheduling() const {return disable_per_scheduling;}
    bool get_split_trace() const {return split_trace;}
    bool contains(const std::string& name) const {
      if (options.find(name) != options.end()) {
        return true;
      } else {
        return false;
      }
    }

    void add (const std::string& name, const std::string& value) {
      if (!contains(name)) {
        options.insert(make_pair(name, value));
      } else {
        printf("ramulator::Config::add options[%s] already set.\n", name.c_str());
      }
    }

    void set(const std::string& name, const std::string& value) {
      options[name] = value;
      // TODO call this function only name maps to a constant
      parse_to_const(name, value);
    }

    void set_core_num(int _core_num) {core_num = _core_num;}
    void set_cacheline_size(int _cacheline_size) {cacheline_size = _cacheline_size;}


    int get_int_value(const std::string& name) const {
      assert(options.find(name) != options.end() && "can't find this argument");
      return atoi(options.find(name)->second.c_str());
    }
    int get_stacks() const {return get_int_value("stacks");}
    int get_channels() const {return get_int_value("channels");}
    int get_subarrays() const {return get_int_value("subarrays");}
    int get_ranks() const {return get_int_value("ranks");}
    bool pim_mode_enabled () const {if(get_int_value("pim_mode") == 1) return true; return false;}
    Format get_trace_format() const{return format;}
    std::string get_cpu_type() const{
       /*if (contains("core_type")) {
           return options.find("core_type")->second;
       }*/
       return org;
    }
    int get_cpu_tick() const {return int(1000000.0 / get_int_value("cpu_frequency"));}
    int get_core_num() const {return core_num;}
    int get_cacheline_size() const {return cacheline_size;}
    long get_expected_limit_insts() const {
      if (contains("expected_limit_insts")) {
        return get_int_value("expected_limit_insts");
      } else {
        return 0;
      }
    }
    bool has_l3_cache() const {
      if (options.find("cache") != options.end()) {
        const std::string& cache_option = (options.find("cache"))->second;
        return (cache_option == "all") || (cache_option == "L3");
      } else {
        return false;
      }
    }
    bool has_core_caches() const {
      if (options.find("cache") != options.end()) {
        const std::string& cache_option = (options.find("cache"))->second;
        return (cache_option == "all" || cache_option == "L1L2");
      } else {
        return false;
      }
    }
    bool is_early_exit() const {
      // the default value is true
      if (options.find("early_exit") != options.end()) {
        if ((options.find("early_exit"))->second == "off") {
          return false;
        }
        return true;
      }
      return true;
    }
    bool calc_weighted_speedup() const {
      return (expected_limit_insts != 0);
    }
    bool record_cmd_trace() const {
      // the default value is false
      if (options.find("record_cmd_trace") != options.end()) {
        if ((options.find("record_cmd_trace"))->second == "on") {
          return true;
        }
        return false;
      }
      return false;
    }
    bool print_cmd_trace() const {
      // the default value is false
      if (options.find("print_cmd_trace") != options.end()) {
        if ((options.find("print_cmd_trace"))->second == "on") {
          return true;
        }
        return false;
      }
      return false;
    }
};


} /* namespace ramulator */

#endif /* _CONFIG_H */

