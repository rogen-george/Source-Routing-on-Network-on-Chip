// $Id$

/*
Copyright (c) 2007-2009, Trustees of The Leland Stanford Junior University
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this 
list of conditions and the following disclaimer in the documentation and/or 
other materials provided with the distribution.
Neither the name of the Stanford University nor the names of its contributors 
may be used to endorse or promote products derived from this software without 
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*router.cpp
 *
 *The base class of either iq router or event router
 *contains a list of channels and other router configuration variables
 *
 *The older version of the simulator uses an array of flits and credit to 
 *simulate the channels. Newer version ueses flitchannel and credit channel
 *which can better model channel delay
 *
 *The older version of the simulator also uses vc_router and chaos router
 *which are replaced by iq rotuer and event router in the present form
 */

#include "booksim.hpp"
#include <iostream>
#include <assert.h>
#include "router.hpp"

//////////////////Sub router types//////////////////////
#include "iq_router_baseline.hpp"
#include "iq_router_combined.hpp"
#include "iq_router_split.hpp"
#include "event_router.hpp"
#include "chaos_router.hpp"
#include "MECSRouter.hpp"

///////////////////////////////////////////////////////

Router::Router( const Configuration& config,
		Module *parent, const string & name, int id,
		int inputs, int outputs ) :
  Module( parent, name ),
  _id( id ),
  _inputs( inputs ),
  _outputs( outputs )
{

  _st_prepare_delay = config.GetInt( "st_prepare_delay" );
  _st_final_delay   = config.GetInt( "st_final_delay" );
  _credit_delay     = config.GetInt( "credit_delay" );
  _input_speedup    = config.GetInt( "input_speedup" );
  _output_speedup   = config.GetInt( "output_speedup" );

  _input_channels = new vector<FlitChannel *>;
  _input_credits  = new vector<CreditChannel *>;

  _output_channels = new vector<FlitChannel *>;
  _output_credits  = new vector<CreditChannel *>;

  _channel_faults  = new vector<bool>;
  
 //added by KVM
 //initialise number of flits routed through a router to 0
 _num_of_flits_routed=0;
// *********
  // make a change here if num_vcs are changed in input parameter file
	count_VCs_port=10;

// **********

 
 //initialise number of flits through each port to 0
 for(int i=0;i<5;i++)
	 _num_of_flits_routed_per_port[i]=0;
	 
 //initialise past and present flits through each port
  for(int i=0;i<5;i++)
  {
  	//_cum_past_flits_per_port[i]=0;
  	//_present_flits_per_port[i]=0;
  	_weighted_NOF_per_port[i]=0;
  	wnof_old[i]=0;
	// john added from shiva
	cum_time_in_router[i]=0;
  	cum_time_out_router[i]=0;
  	cum_time_out_this_clock[i]=0;
  	cum_time_out_previous_clock[i]=0;
  	has_input[i]=0;
	for(int j=0;j<count_VCs_port;j++)  //for vcs>4
  		has_input_vc[i][j]=0;
  	//for(int j=0;j<4;j++)
  	//	has_input_vc[i][j]=0;
  }
  
  for(int i=0;i<4;i++)// for # ports
  {
  	free_vcs_old[i]=count_VCs_port;                            // change to #vcs
  	free_vcs_new[i]=count_VCs_port;			      // change to #vcs
  	free_vcs_inter[i]=count_VCs_port;			       // change to #vcs
  	for(int j=0;j<count_VCs_port;j++)  //4
  	{
		fluidity[i][j]=1;
		new_fluidity[i][j]=1;
		update_fluidity[i][j]=1;
	}
  	//initialize the fluidity of the router to 1


  }
  /*for(int i=0;i<4;i++)  // for num_vcs=4
  {
  	free_vcs_old[i]=4;
  	free_vcs_new[i]=4;
  	free_vcs_inter[i]=4;
  	for(int j=0;j<4;j++)  //4
  	{
		fluidity[i][j]=1;
		new_fluidity[i][j]=1;
		update_fluidity[i][j]=1;
	}
  	//initialize the fluidity of the router to 1


  }*/
  bit_time_per_outport=new list<int> [4];
  //initialise alpha
  alpha=0.25;
	 
 //neighbours of a router in all directions
 _neighbours= new vector<Router *>;
 //cout<< _num_of_flits_routed<<endl;
 for(int i=0;i<4;i++)
 for(int j=0;j<5;j++)
 	bit_time_per_outport[i].push_back(0);

}

Router::~Router( )
{
  delete _input_channels;
  delete _input_credits;
  delete _output_channels;
  delete _output_credits;
  delete _channel_faults;
}
//added by KVM

void Router::update_nop_values()
{
	
	/*for(int i=0;i<4;i++)//for copying (old contain 2 cycle old data.....)
	{
		free_vcs_old[i]=free_vcs_inter[i];
		free_vcs_inter[i]=free_vcs_new[i];
	}*/

	for(int i=0;i<4;i++)//for copying (old contain 1 cycle old data.....)
	{
		free_vcs_old[i]=free_vcs_new[i];
		//free_vcs_inter[i]=free_vcs_new[i];
	}
	free_vcs_all_ports(free_vcs_new);
	
}
int* Router::get_free_vcs_old()
{
	return free_vcs_old;
}
void Router::IncrementNOF()
{
	_num_of_flits_routed++;
}
int Router::GetNOF()
{
	return _num_of_flits_routed;
}
void Router::calc_wnof_all_ports() // not used at all...............
{ // not used at all...............
	for(int p=0;p<4;p++)
	{
		//cout<<_present_flit_at_port[p]<<endl;  // not used
		//_weighted_NOF_per_port[p]=_weighted_NOF_per_port[p]+_present_flits_per_port[p];
		//_present_flits_per_port[p]=0;
		//_weighted_NOF_per_port[p]=(_present_flits_per_port[p])+(alpha*_cum_past_flits_per_port[p]);
		/*if(_weighted_NOF_per_port[p]>15)
			_weighted_NOF_per_port[p]=15;*/
	}
	//getchar();
}
void Router::update_wnof_values()
{
	for(int i=0;i<4;i++)//for copying16
	{
		wnof_old[i]=_weighted_NOF_per_port[i];
	} 
	//calc_wnof_all_ports();
}
int* Router::get_wnof_old()
{
	return wnof_old;
}

void Router::IncrementNOF_port(int port)
{
	_num_of_flits_routed_per_port[port]++;
}
int Router::GetNOF_port(int port)
{
	return _num_of_flits_routed_per_port[port];
}
int Router::GetNOF_mean(vector<int> neighbour_ports)
{
	vector<int>::iterator p;
	int sum_flits=0,mean_flits=0;
	for(p=neighbour_ports.begin();p<neighbour_ports.end();p++)
	{
			/*if(f->id==28760)
			{
				fp<<"port:"<<*p<<endl;
				fp<<"num of flits"<<next_router->GetNOF_port(*p)<<endl;
			}*/
			sum_flits=sum_flits+_num_of_flits_routed_per_port[(*p)];
			
	}
		mean_flits=sum_flits/neighbour_ports.size();
		return mean_flits;
		//try with square root of mean flits
}

/*int* Router::GetNOF_weighted_neighbours()
{
	for(int i=0;i<4;i++)
	{
		_weighted_NOF_per_port[i]=(_present_flits_per_port[i])+(alpha*_cum_past_flits_per_port[i]);
	}
	
	return (_weighted_NOF_per_port);
}*/
int* Router::GetNOF_neighbours()
{	
	return (_num_of_flits_routed_per_port);
}

void Router::Update_cum_flits()  // updation of metric with alpha (if alpha=0.25 divide by 4)
{
	for(int i=0;i<5;i++)
  	{
  	/*_cum_past_flits_per_port[i]=(alpha*_cum_past_flits_per_port[i])+(_present_flits_per_port[i]);
  	if(_cum_past_flits_per_port[i]>31)
  		_cum_past_flits_per_port[i]=31;*/
  	//_present_flits_per_port[i]=0;
	//cout<<"johneeee"<<endl;
	//getchar();
	_weighted_NOF_per_port[i]=_weighted_NOF_per_port[i]/4;  // (if alpha=0.25 divide by 4)
	// _weighted_NOF_per_port[i]=_weighted_NOF_per_port[i]*0.6;  // (to change alpha values)
  	if(_weighted_NOF_per_port[i]>15)  // no need to check size limit
  		_weighted_NOF_per_port[i]=15;
  	
  	}  
}

void Router::Increment_present_flits(int port) // updation of wnof at Flit movement (TRACKER modified)
{
	/*if(_present_flits_per_port[port]<15)
	_present_flits_per_port[port]++;*/
	//_present_flits_per_port=1;
	_weighted_NOF_per_port[port]++;
	if(_weighted_NOF_per_port[port]>15)  // 31 for 5 bit   size of SR change here
  		_weighted_NOF_per_port[port]=15;
}

// ********  for BOFAR and fluidity (from shivasankar)********
void Router::Update_cum_time_out()
{
	int temp=0;
	for(int i=0;i<4;i++)
	{   // computation of delta and size of encoded delta
		
		temp=cum_time_out_this_clock[i]-cum_time_out_previous_clock[i];  
		// comment the following "if condition" if whole 8 bit is to be transferred as control metric
		// The comparing value inside if limits the size of congestion metric captured.
		
		if(temp>7)   // 7 --> 3 bit adn 15--> 4 bit
		{
			temp=7;
		}
				
		cum_time_out_router[i]=cum_time_out_router[i]+temp;
		if(cum_time_out_router[i] >63)    // size of SR.... SR=6 bit std..
			cum_time_out_router[i]=63;
		cum_time_out_previous_clock[i]=cum_time_out_this_clock[i];
	}
}

void Router::Clear_has_input()
{
	for(int i=0;i<5;i++)
	{
		has_input[i]=0;
		for(int j=0;j<count_VCs_port;j++) // change for num_vcs
			has_input_vc[i][j]=0;
	}
}
void Router::Clear_cum_in_time()
{
	for(int i=0;i<5;i++)
		cum_time_in_router[i]=0;
}
void Router::Clear_cum_out_time()
{
	for(int i=0;i<5;i++)
	{
		//

		cum_time_out_router[i]=cum_time_out_router[i]*0.125;  //alpha values
		cum_time_out_this_clock[i]=0;
		cum_time_out_previous_clock[i]=0;
	}	
	
}

list<int> * Router::get_time_in_bits()
{
	return bit_time_per_outport;
}
int* Router::get_in_time()
{
	return cum_time_in_router;
}
int* Router::get_out_time()
{
	return cum_time_out_router;
}
int* Router::get_inport_util()
{
	return flits_per_inport;
}
int* Router::get_outport_util()
{
	return flits_per_outport;
}
void Router::Update_fluidity()
{
	for(int i=0;i<4;i++)
	{
		for(int j=0;j<count_VCs_port;j++)  // change for num_vcs
		{
			fluidity[i][j]=new_fluidity[i][j];
			new_fluidity[i][j]=update_fluidity[i][j];
		}
	}
}

// **************** end shivasanker *****************



Credit *Router::_NewCredit( int vcs )
{
  Credit *c;
  //cout<<"new credit created"<<endl;
  //getchar();

  c = new Credit( vcs );
  return c;
}

void Router::_RetireCredit( Credit *c )
{
  delete c;
}

void Router::AddInputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  _input_channels->push_back( channel );
  _input_credits->push_back( backchannel );
  channel->SetSink( this ) ;//in flitchannel.cpp
}

void Router::AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel )
{
  _output_channels->push_back( channel );
  _output_credits->push_back( backchannel );
  _channel_faults->push_back( false );
  channel->SetSource( this ) ;//in flitchannel.cpp
}

int Router::GetID( ) const
{
  return _id;
}


void Router::OutChannelFault( int c, bool fault )
{
  assert( ( c >= 0 ) && ( (unsigned int)c < _channel_faults->size( ) ) );

  (*_channel_faults)[c] = fault;
}

bool Router::IsFaultyOutput( int c ) const
{
  assert( ( c >= 0 ) && ( (unsigned int)c < _channel_faults->size( ) ) );

  return (*_channel_faults)[c];
}

/*Router constructor*/
Router *Router::NewRouter( const Configuration& config,
			   Module *parent, string name, int id,
			   int inputs, int outputs )
{
//cout<<"nr"<<endl;

  Router *r;
  string type;
  string topo;

  config.GetStr( "router", type );
  config.GetStr( "topology", topo);
	//cout<<"type"<<type<<endl;
  if ( type == "iq" ) {
    if(topo == "MECS"){
      r = new MECSRouter( config, parent, name, id, inputs, outputs);
    } else {
      r = new IQRouterBaseline( config, parent, name, id, inputs, outputs );
    }
  } else if ( type == "iq_combined" ) {
    r = new IQRouterCombined( config, parent, name, id, inputs, outputs );
  } else if ( type == "iq_split" ) {
    r = new IQRouterSplit( config, parent, name, id, inputs, outputs );
  } else if ( type == "event" ) {
    r = new EventRouter( config, parent, name, id, inputs, outputs );
  } else if ( type == "chaos" ) {
    r = new ChaosRouter( config, parent, name, id, inputs, outputs );
  } else {
    cout << "Unknown router type " << type << endl;
  }
  /*For additional router, add another else if statement*/
  /*Original booksim specifies the router using "flow_control"
   *we now simply call these types. 
   */
//cout<<"enr"<<endl;
  return r;
}





