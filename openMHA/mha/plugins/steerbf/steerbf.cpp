// This file is part of the HörTech Open Master Hearing Aid (openMHA)
// Copyright © 2014 2015 2016 2017 2018 2019 2021 HörTech gGmbH
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

#include "steerbf.h"

#define PATCH_VAR(var) patchbay.connect(&var.valuechanged, this, &steerbf::update_cfg)
#define INSERT_PATCH(var) insert_member(var); PATCH_VAR(var)

steerbf_config::steerbf_config(MHA_AC::algo_comm_t & ac,
                               const mhaconfig_t in_cfg,
                               steerbf *steerbf) :
  nchan( in_cfg.channels ),
    nfreq( in_cfg.fftlen/2 + 1 ),
    outSpec( nfreq, 1 ), //mono output
    bf_vec(  ),
    //set nangle by counting/inferring number of blocks
    nangle( bf_vec.num_channels / nchan ),
    _steerbf( steerbf ), ac(ac),
  bf_src_copy( steerbf->bf_src.data )
{
    //set the correct upper limit given data
    steerbf->angle_ind.set_max_angle_ind( nangle-1 );
}

steerbf_config::~steerbf_config() {}

//initialise global variables for the fixed beam and calibrate north value.
int fixed_beam_value = 0;
int calibrate_north_value = 0;

//global help functions
int convert_positive_degree(int value, int max_degree){
    if (value < 0) value = (value % max_degree) + max_degree;
    return value;
}

int set_calibrate_north(int value, int max_degree){
    int calibrated_value = (value - calibrate_north_value) % max_degree;
    return convert_positive_degree(calibrated_value, max_degree);
}


/* live processing class */
mha_spec_t *steerbf_config::process(mha_spec_t *inSpec)
{
    float head_angle_float = 0.0;
    bf_vec = MHA_AC::get_var_spectrum(ac, bf_src_copy );
    //if angle_src is set, then retrieve steering from AC variable
    //otherwise use the configuration variable
    int angle_ind;
    if ( _steerbf->angle_src.data.compare("") != 0 ) {
        // angle_ind = MHA_AC::get_var_int(ac, _steerbf->angle_src.data );
        const mha_wave_t angle_ind_wave = MHA_AC::get_var_waveform(ac, _steerbf->angle_src.data);
        angle_ind = (int)value(angle_ind_wave, 0, 0);
    }
    //IF ANGLE_DEGREE IS SET CONVERT OSC DEGREE ANGLE TO REQUIRED INDEX
    else if ( _steerbf->angle_degree.data.compare("") != 0 ) {

        const float max_degree = 360.0;
        //Convert ac variable to accessible value
        const mha_wave_t beam_input_data = MHA_AC::get_var_waveform(ac, _steerbf->angle_degree.data);



        //CHECK IF HEAD TRACKER IS INITIALISED. SET BEAM TO FIXED DIRECTION TO WORLD COORDINATES
        if(_steerbf->head_angle.data.compare("") != 0 ) {
            // const mha_wave_t head_input_data = MHA_AC::get_var_waveform(ac, _steerbf->head_angle.data);
            // head_angle_float = value(head_input_data, 0, 0);
            const std::vector head_input_data = MHA_AC::get_var_vfloat(ac, _steerbf->head_angle.data);

            
            //Set calibrated north value
            if(_steerbf->calibrate_north.data.compare("") != 0){
                if((int)value(MHA_AC::get_var_waveform(ac, _steerbf->calibrate_north.data), 0, 0) ){
                    calibrate_north_value = head_input_data[0];
                }
            } 

            head_angle_float = set_calibrate_north(head_input_data[0], max_degree);

            //CHECK IF FLIP HEAD ORIENTATION IS TRIGGERED. FLIP THE HEAD ORIENTATION
            if(_steerbf->flip_head.data == 1 ){
                head_angle_float = (int)(-head_angle_float + max_degree) % (int)max_degree;
            }
            
        }

        //CHECK IF FIX BEAM IS INITIALISED. SET BEAM TO FIXED DIRECTION TO HEAD TRACKER
        int degree_value_int = 0;
        if(_steerbf->fix_beam.data.compare("") != 0 ) {
            bool set_fix_beam = (int)value(MHA_AC::get_var_waveform(ac, _steerbf->fix_beam.data), 0, 0);
            if( set_fix_beam){
                //fixed_beam_value is already in MHA standard
                degree_value_int = fixed_beam_value;
            }else{
                //Convert data to integer between 0 and 360
                //The degree Value will always be with respect to NORTH
                //The addition of 180 is to normalise starting point of GUI 0 North to mid index in MHA 
                int beam_head_data = convert_positive_degree((int)value(beam_input_data, 0, 0) - head_angle_float, (int)max_degree);
                degree_value_int = (beam_head_data + 180) % (int)max_degree;
                //Set the possible fixed beam value
                fixed_beam_value = degree_value_int;
            }
        }        



        //Number of total indices available by filter
        nangle = bf_vec.num_channels / nchan;
        //Number of indices per degree
        float index_per_deg = (nangle-1) / max_degree;
        //Correct index value for given degree_val_int
        float index_val = index_per_deg * degree_value_int;
        //Round value to suitable integer
        angle_ind = round(index_val);

    }
    else {
        angle_ind = _steerbf->angle_ind.data;
    }
    int block_ind = angle_ind*nchan;

    // std::cout << _steerbf->bf_src.data << std::endl;

    //do the filtering and summing
    for (unsigned int f=0; f<nfreq; f++) {

        // std::cout << "freq: " << f << std::endl;
        //init output to zero
        outSpec(f,0).re = 0;
        outSpec(f,0).im = 0;

        for (unsigned int m=0; m<nchan; ++m) {
            //outSpec(f,0) += _conjugate((*beamW)(f,m)) * value(inSpec,f,m);
            outSpec(f,0) += _conjugate(value(bf_vec,f,m+block_ind)) * value(inSpec,f,m);
            // std::cout << "c: " << m << ": " << value(bf_vec,f,m+block_ind).re << " " << value(bf_vec,f,m+block_ind).im << std::endl;
        }
    }

    
    _steerbf->head_angle_float = head_angle_float;
    _steerbf->insert();
    return &outSpec;
}



/** Constructs our plugin. */
steerbf::steerbf(MHA_AC::algo_comm_t & iac, const std::string & configured_name)
    : MHAPlugin::plugin_t<steerbf_config>("Steerable Beamformer",iac),
      bf_src("Provides the beamforming filters encoded as a block matrix: [chanXnangle,nfreq].", ""),
      angle_ind("Sets the steering angle in filtering.", "0", "[0,1000]"),
      angle_src("If initialized, provides an int-AC variable of steering index.",""),

      angle_degree("If initialized, provides an int-AC variable of steering angle.",""),
      calibrate_north("If initialized, provides an int-AC variable for calibrating head tracker to north.",""),
      head_angle("If initialized, provides an int-AC variable of head tracking angle.",""),
      fix_beam("If initialized, provides an int-AC variable fixing the beam respective of head direction.",""),
      flip_head("If true, flips the orientation for the received head angle.","0", "[0, 1]"),
      algo(configured_name)
{
    //only make a new configuration when bf_src changes
    INSERT_PATCH(bf_src);

    //otherwise, the processing plugins query for the current angles
    insert_member(angle_ind);
    insert_member(angle_src);
    //Insert custom member variables
    insert_member(angle_degree);
    insert_member(calibrate_north);
    insert_member(head_angle);
    insert_member(fix_beam);
    insert_member(flip_head);

    insert();
}

void steerbf::insert() {
    ac.insert_var_float("acHeadAngleConverted" + algo, &head_angle_float);
}


steerbf::~steerbf() {}

/** Plugin preparation.
 *  An opportunity to validate configuration parameters before instantiating a configuration.
 * @param signal_info
 *   Structure containing a description of the form of the signal (domain,
 *   number of channels, frames per block, sampling rate.
 */
void steerbf::prepare(mhaconfig_t & signal_info)
{
    //good idea: restrict input type and dimension
    /*
    if (signal_info.channels != 2)
        throw MHA_Error(__FILE__, __LINE__,
                        "This plugin must have 2 input channels: (%d found)\n"
                        "[Left, Right].", signal_info.channels);
                        */
    if (signal_info.domain != MHA_SPECTRUM)
        throw MHA_Error(__FILE__, __LINE__,
                        "This plugin can only process spectrum signals.");

    //set output dimension
    signal_info.channels = 1;

    /* make sure that a valid runtime configuration exists: */
    update_cfg();
    insert();
}

void steerbf::update_cfg()
{
    if ( is_prepared() ) {

        //when necessary, make a new configuration instance
        //possibly based on changes in parser variables
        steerbf_config *config;
        config = new steerbf_config( ac, input_cfg(), this );
        push_config( config );
    }
}

/**
 * Defers to configuration class.
 */
mha_spec_t * steerbf::process(mha_spec_t * signal)
{
    //this stub method defers processing to the configuration class
    return poll_config()->process( signal );
}

/*
 * This macro connects the plugin1_t class with the MHA plugin C interface
 * The first argument is the class name, the other arguments define the
 * input and output domain of the algorithm.
 */
MHAPLUGIN_CALLBACKS(steerbf,steerbf,spec,spec)

/*
 * This macro creates code classification of the plugin and for
 * automatic documentation.
 *
 * The first argument to the macro is a space separated list of
 * categories, starting with the most relevant category. The second
 * argument is a LaTeX-compatible character array with some detailed
 * documentation of the plugin.
 */
MHAPLUGIN_DOCUMENTATION\
(steerbf,
 "filter spatial audio-channels beamforming binaural",
 "Implements frequency-domain beamformer processing (filter and sum) using"
 " externally provided filters. "
 "A plugin called {\\tt acSteer} can be used to provide the filter coefficients. "
 "The filter coefficients to be read are saved as a waveform object in the AC space. "
 "Each channel of this object corresponds to a different steering angle."
 " The steering angle is typically determined in real-time by a"
 " localization plugin (e.g. {\\tt doasvm\\_classification}). "
 "In this case, the index to the corresponding steering direction is read"
 " from the AC space."
 " Note that the number of available filters should be consistent with"
 " the number of possible steering directions to be estimated."
 " The configuration variable \\textbf{angle\\_src} keeps the name of the"
 " AC variable for the estimated steering direction. "
 "The steering angle can also be fixed in the configuration time using the"
 " configuration variable \\textbf{angle\\_ind}."
 )


/*
 * Local Variables:
 * compile-command: "make"
 * indent-tabs-mode: nil
 * c-basic-offset: 4
 * coding: utf-8-unix
 * End:
 */
