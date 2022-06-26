// This file is part of the HörTech Open Master Hearing Aid (openMHA)
// Copyright © 2018 2019 2020 2021 HörTech gGmbH
// Copyright © 2022 Hörzentrum Oldenburg gGmbH
//
// openMHA is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, version 3 of the License.
//
// openMHA is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License, version 3 for more details.
//
// You should have received a copy of the GNU Affero General Public License, 
// version 3 along with openMHA.  If not, see <http://www.gnu.org/licenses/>.

#include "mha_plugin.hh"
#include "mha_os.h"
#include "mha_events.h"
#include "mha_defs.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "lsl_cpp.h"
#pragma GCC diagnostic pop

#include <memory>
#include <map>
#include <pthread.h>
#include <sched.h>

/** All types for the ac2lsl plugins live in this namespace. */
namespace ac2lsl{

    struct type_info{
        const std::string name;
        const lsl::channel_format_t format;
    };

    const std::map<int, type_info>
    types={
        {MHA_AC_INT,{"MHA_AC_INT",lsl::cf_int32}},
        {MHA_AC_FLOAT,{"MHA_AC_FLOAT",lsl::cf_float32}},
        {MHA_AC_DOUBLE,{"MHA_AC_DOUBLE",lsl::cf_double64}},
        {MHA_AC_MHAREAL,{"MHA_AC_MHAREAL",lsl::cf_float32}},
        {MHA_AC_MHACOMPLEX,{"MHA_AC_MHACOMPLEX",lsl::cf_float32}}
    };

    /** Interface for ac to lsl bridge variable*/
    class save_var_base_t{
    public:
        virtual void send_frame(unsigned num_entries)=0;
        virtual void* get_buf_address() const noexcept = 0;
        virtual void set_buf_address(void* data) = 0;
        virtual lsl::stream_info info() const noexcept = 0;
        virtual unsigned data_type() const noexcept = 0;
        virtual ~save_var_base_t()=default;
    };

    /** Implementation for all ac to lsl bridges except complex types. */
    template<typename T>
    class save_var_t : public save_var_base_t {
    public:
        /** C'tor of generic ac to lsl bridge.
         * @param info LSL stream info object containing metadata
         * @param data Pointer to data buffer of the ac variable
         * @param data_type Type id of the stream, in mha convention.
         Should be set to one if not a vector.
        */
        save_var_t(const std::string& name_,const std::string& type_, unsigned num_channels_,
                   const mha_real_t rate_, const lsl::channel_format_t format_, const std::string& source_id_,
                   void* data_, const unsigned data_type_):
            stream(lsl::stream_info(name_, type_, num_channels_, rate_, format_, source_id_)),
            //AC variables hold the addresses as void ptr, we find out the type from
            //metadata and call templated constructor.
            buf(static_cast<T*>(data_)),
            data_type_(data_type_)
        {};
        /** Get buffer address as void pointer
         * @returns Adress of the data buffer
         */
        virtual void* get_buf_address() const noexcept override {
            return static_cast<void*>(buf);
        };
        /** Cast the input pointer to the appropriate type and set the buffer address
         * @param data New buffer address
         */
        virtual void set_buf_address(void* data) override {
            buf=static_cast<decltype(buf)>(data);
        };
        /** Get stream info object from stream outlet */
        virtual lsl::stream_info info() const noexcept override {
            return stream.info();
        };
        /** Get data type id according MHA convention*/
        virtual unsigned data_type() const noexcept override{
            return data_type_;
        }
        virtual ~save_var_t()=default;
        /** Send a frame to lsl. */
        virtual void send_frame(unsigned num_entries) override {stream.push_chunk_multiplexed(buf,num_entries);};
    private:
        /** LSL stream outlet. Interface to lsl */
        lsl::stream_outlet stream;
        /** Pointer to data buffer of the ac variable. */
        T* buf;
        /** Data type id according to MHA convention. */
        const unsigned data_type_;
    };


    /** Template specialization of the ac2lsl bridge to take care of complex
     * numbers. This specialization is needed because lsl does not support
     * complex numbers.
     * Order is [re(0), im(0), re(1), im(1), ....]
     */
    template<>
    class save_var_t<mha_complex_t> : public save_var_base_t {
    public:
        /** C'tor of specialization for complex types.
         * See generic c'tor for details. */
        save_var_t(const std::string& name_,const std::string& type_, unsigned num_channels_,
                   const mha_real_t rate_, const lsl::channel_format_t format_, const std::string& source_id_,
                   void* data_):
            stream(lsl::stream_info(name_, type_, num_channels_*2, rate_, format_, source_id_)),
            //AC variables hold the addresses as void ptr, we find out the type from
            //metadata and call templated constructor.
            buf(static_cast<mha_complex_t*>(data_))
        {};
        virtual void* get_buf_address() const noexcept override {
            return static_cast<void*>(buf);
        };
        virtual void set_buf_address(void* data) override {
            buf=static_cast<mha_complex_t*>(data);
        };
        /** Get buffer address as void pointer
         * @returns Adress of the data buffer
         */
        virtual lsl::stream_info info() const noexcept override {
            return stream.info();
        };
        /** Cast the input pointer to the appropriate type and set the buffer address
         * @param data New buffer address
         */
        virtual unsigned data_type() const noexcept override {
            return MHA_AC_MHACOMPLEX;
        }
        virtual ~save_var_t()=default;
        /** Send a frame of complex types.
         * Complex numbers are stored as alternating real and imaginary parts.
         * An array of complex numbers in memory can be reinterpreted as a
         * vector of real numbers that correspond to real and imaginary parts.
         * LSL does not support complex types directly. Send one vector
         * containing {buf[0].re,buf[0].im,buf[1].re,buf[1].im,...} instead. */
        virtual void send_frame(unsigned num_entries) override {
            stream.push_chunk_multiplexed(&buf[0].re,num_entries*2);
        };
    private:
        /** LSL stream outlet. Interface to lsl */
        lsl::stream_outlet stream;
        /** Pointer to data buffer of the ac variable. */
        mha_complex_t* buf;
    };

    /** Runtime configuration class of the ac2lsl plugin */
    class cfg_t {
        void create_or_replace_var(const std::string& name,
                                   const MHA_AC::comm_var_t& v);
        void check_and_send();
        /** Maps variable name to unique ptr's of ac to lsl bridges. */
        std::map<std::string, std::unique_ptr<save_var_base_t>> varlist;
        /** Counter of frames to skip */
        unsigned skipcnt;
        /** Number of frames to skip after each send */
        const unsigned skip;
        /** Sampling rate of the stream */
        const double srate;
        /** User configurable source id. */
        const std::string source_id;
        /** Handle to the ac space*/
        const MHA_AC::algo_comm_t & ac;
    public:

        /** C'tor of ac2lsl run time configuration
         * @param ac_         AC space, source of data to send over LSL
         * @param skip_       Number of frames to skip after each send
         * @param source_id_  LSL identifier for this data stream
         * @param varnames_   Names of AC variables to send over LSL
         * @param rate        Rate with wich chunks of data are sent to the LSL
         *                    stream.  Usually the rate with which process calls
         *                    happen, but may be lower due to the subsampling
         *                    caused by skip_ */
        cfg_t(MHA_AC::algo_comm_t & ac_,
              unsigned skip_,
              const std::string& source_id,
              const std::vector<std::string>& varnames_,
              double rate);
        void process();

    };

    /** Plugin class of ac2lsl */
    class ac2lsl_t : public MHAPlugin::plugin_t<cfg_t>
    {
    public:
        ac2lsl_t(MHA_AC::algo_comm_t & iac,
                 const std::string & configured_name);
        /** Prepare constructs the vector of bridge variables and locks
         * the configuration, then calls update(). */
        void prepare(mhaconfig_t&);
        /** Processing fct for waveforms. Calls process(void). */
        mha_wave_t* process(mha_wave_t* s) {process();return s;};
        /** Processing fct for spectra. Calls process(void). */
        mha_spec_t* process(mha_spec_t* s) {process();return s;};
        /** Process function. Checks once if the plugin is run in a
         * real-time thread and throws if rt_strict is true,
         * then forwards to cfg_t::process(). */
        void process();
        /** Release fct. Unlocks variable name list */
        void release();
    private:
        /** Construct new runtime configuration */
        void update();
        MHAParser::vstring_t vars;
        MHAParser::string_t source_id;
        MHAParser::bool_t rt_strict;
        MHAParser::bool_t activate;
        MHAParser::int_t skip;
        MHAParser::float_t nominal_srate;
        MHAEvents::patchbay_t<ac2lsl_t> patchbay;
        bool is_first_run;
    };
}

ac2lsl::ac2lsl_t::ac2lsl_t(MHA_AC::algo_comm_t & iac, const std::string &)
    : MHAPlugin::plugin_t<ac2lsl::cfg_t>("Send AC variables as"
                                         " LSL messages.",iac),
    vars("List of AC variables to be saved, empty for all.","[]"),
    source_id("Unique source id for the stream outlet.",""),
    rt_strict("Abort if used in real-time thread?","yes"),
    activate("Send frames to network?","yes"),
    skip("Number of frames to skip after sending","0","[0,]"),
    nominal_srate("Nominal sampling rate of AC variables","0","[0,]"),
    is_first_run(true)
{
    insert_member(vars);
    insert_member(source_id);
    insert_member(rt_strict);
    insert_member(activate);
    insert_member(nominal_srate);
    insert_member(skip);
    //Nota bene: Activate should not be connected to the patchbay because we skip processing
    //in the plugin class when necessary. If activate used update() as callback, streams
    //would get recreated everytime activate is toggled.
    patchbay.connect(&source_id.writeaccess,this,&ac2lsl_t::update);
    patchbay.connect(&rt_strict.writeaccess,this,&ac2lsl_t::update);
    patchbay.connect(&nominal_srate.writeaccess,this,&ac2lsl_t::update);
    patchbay.connect(&skip.writeaccess,this,&ac2lsl_t::update);
    patchbay.connect(&vars.writeaccess,this,&ac2lsl_t::update);
}

void ac2lsl::ac2lsl_t::prepare(mhaconfig_t&)
{
    try {
        vars.setlock(true);
        rt_strict.setlock(true);
        //No variable names were given in the configuration,
        //meaning we have to scan the whole ac space
        if( !vars.data.size() ){
            vars.data=ac.get_entries();
        }
        update();
    }
    catch(MHA_Error& e){
        vars.setlock(false);
        rt_strict.setlock(false);
        throw;
    }
}

void ac2lsl::ac2lsl_t::release()
{
    is_first_run=true;
    rt_strict.setlock(false);
    vars.setlock(false);
}

void ac2lsl::ac2lsl_t::process()
{
    if(is_first_run){
        if(rt_strict.data)
            {
                is_first_run=false;
                pthread_t this_thread=pthread_self();
                int policy=0;
                struct sched_param params;
                auto ret=pthread_getschedparam(this_thread,&policy,&params);
                if(ret != 0)
                    throw MHA_Error(__FILE__,__LINE__,"could not retrieve"
                                    " thread scheduling parameters!");
                if(policy == SCHED_FIFO or policy==SCHED_RR)
                    throw MHA_Error(__FILE__,__LINE__,"ac2lsl used in"
                                    " real-time thread with"
                                    " rt-strict=true!");
            }
    }
    poll_config();
    if(activate.data)
        cfg->process();
}

void ac2lsl::ac2lsl_t::update(){
    if(is_prepared()){
      auto c=new cfg_t(ac, skip.data, source_id.data, vars.data,nominal_srate.data);
        push_config(c);
    }
}

ac2lsl::cfg_t::cfg_t(MHA_AC::algo_comm_t & ac_,
                     unsigned skip_,
                     const std::string& source_id_,
                     const std::vector<std::string>& varnames_,
                     double rate_):
    skipcnt(skip_),
    skip(skip_),
    srate(rate_),
    source_id(source_id_),
    ac(ac_)
{
    for(auto& name : varnames_) {
        MHA_AC::comm_var_t v = ac.get_var(name);
        create_or_replace_var(name, v);
    }
}

void ac2lsl::cfg_t::process(){
    if(!skipcnt){
        check_and_send();
        skipcnt=skip;
    }
    else{
        skipcnt--;
    }
}

void ac2lsl::cfg_t::check_and_send() {
    for(auto& var : varlist){
        MHA_AC::comm_var_t v = ac.get_var(var.first);
        if( var.second->get_buf_address()!=v.data and
            // static_cast is safe b/c LSL stores channel count as uint32_t
            static_cast<unsigned>(var.second->info().channel_count()) == v.stride and
            var.second->data_type()==v.data_type){
            var.second->set_buf_address(v.data);
        }
        if( var.second->data_type()!=v.data_type) {
            create_or_replace_var(var.first,v);
        }
        // static_cast is safe b/c LSL stores channel count as uint32_t
        if(static_cast<unsigned>(var.second->info().channel_count()) != v.stride){
            create_or_replace_var(var.first,v);
        }
        var.second->send_frame(v.num_entries);
    }
}

void ac2lsl::cfg_t::create_or_replace_var(const std::string& name,
                                          const MHA_AC::comm_var_t& v) {
    unsigned channel_count = v.stride ? v.stride : 1;
    switch( v.data_type ){
    case MHA_AC_INT :
        varlist[name]=std::make_unique<save_var_t<int>>(name,types.at(MHA_AC_INT).name,channel_count,
                                                        srate,types.at(MHA_AC_INT).format,source_id,
                                                        v.data,v.data_type);
        break;
    case MHA_AC_FLOAT :
        varlist[name]=std::make_unique<save_var_t<float>>(name,types.at(MHA_AC_FLOAT).name,channel_count,
                                                          srate,types.at(MHA_AC_FLOAT).format,source_id,
                                                          v.data,v.data_type);
        break;
    case MHA_AC_DOUBLE :
        varlist[name]=std::make_unique<save_var_t<double>>(name,types.at(MHA_AC_DOUBLE).name,channel_count,
                                                           srate,types.at(MHA_AC_DOUBLE).format,source_id,
                                                           v.data,v.data_type);
        break;
    case MHA_AC_MHAREAL :
        varlist[name]=std::make_unique<save_var_t<mha_real_t>>(name,types.at(MHA_AC_MHAREAL).name,channel_count,
                                                               srate,types.at(MHA_AC_MHAREAL).format,source_id,
                                                               v.data,v.data_type);
        break;
    case MHA_AC_MHACOMPLEX :
        varlist[name]=std::make_unique<save_var_t<mha_complex_t>>(name,types.at(MHA_AC_MHACOMPLEX).name,channel_count,
                                                                  srate,types.at(MHA_AC_MHACOMPLEX).format,source_id,
                                                                  v.data);
        break;
    default:
        throw MHA_Error(__FILE__,__LINE__,
                        "Unknown data type: \"%u\"",v.data_type);
    }
}

MHAPLUGIN_CALLBACKS(ac2lsl,ac2lsl::ac2lsl_t,wave,wave)
MHAPLUGIN_PROC_CALLBACK(ac2lsl,ac2lsl::ac2lsl_t,spec,spec)
MHAPLUGIN_DOCUMENTATION\
(ac2lsl,
 "data-export network-communication lab-streaming-layer",
 "This plugin provides a mechanism"
 " to send ac variables over the network using the lab"
 " streaming layer (lsl). If no source id is set,\n"
 " recovery of the stream after changing channel count,\n"
 " data type, or any configuration variable is not possible.\n"
 " Sending data over the network is not real-time safe and\n"
 " processing will be aborted if this plugin is used in a\n"
 " real-time thread without user override."
 " Currently no user-defined types are supported.")

/*
 * Local variables:
 * c-basic-offset: 4
 * compile-command: "make"
 * End:
 */
