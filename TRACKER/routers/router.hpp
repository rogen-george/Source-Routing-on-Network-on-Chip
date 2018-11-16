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

#ifndef _ROUTER_HPP_
#define _ROUTER_HPP_

#include <string>
#include <vector>
#include <math.h>
#include "module.hpp"
#include "flit.hpp"
#include "credit.hpp"
#include "flitchannel.hpp"
#include "channel.hpp"
#include "config_utils.hpp"
#include "booksim_config.hpp"
#include <list>

typedef Channel<Credit> CreditChannel;
union fluid_info
{
mutable	int vc1:1;
mutable int vc2:1;
mutable	int vc3:1;
mutable int vc4:1;
mutable	int x; //vc1=1 indicated vc1 is fluid
};
class Router : public Module {

protected:
  int _id;
  
  int _inputs;
  int _outputs;
  
  int _input_speedup;
  int _output_speedup;
  
  int _st_prepare_delay;
  int _st_final_delay;
  
  int _credit_delay;
  int _num_of_flits_routed;//added by KVM
  int _num_of_flits_routed_per_port[5];//added by KVM
  // int _cum_past_flits_per_port[5];//added by KVM
  //int _present_flits_per_port[5];//added by KVM
  int _weighted_NOF_per_port[5];//added by kvm
  int wnof_old[5];
  float alpha;//added by KVM
  
  int free_vcs_old[5];
  int free_vcs_new[5];
  int free_vcs_inter[4];
 //************************************   shankar ******************************//
  int cum_time_in_router[5];
  int cum_time_out_router[5];
  int cum_time_out_this_clock[5];
  int cum_time_out_previous_clock[5];
  int flits_per_inport[5];
  int flits_per_outport[5];
  //int bit_time_per_outport[20];
  list<int> * bit_time_per_outport;
  
  vector<FlitChannel *>   *_input_channels;
  vector<CreditChannel *> *_input_credits;
  vector<FlitChannel *>   *_output_channels;
  vector<CreditChannel *> *_output_credits;
  vector<bool>            *_channel_faults;

  Credit *_NewCredit( int vcs = 1 );
  void    _RetireCredit( Credit *c );

public:
  int count_VCs_port;
	//union fluid_info fluidity[5];
  int update_fluidity[4][20];    // second index is count of vcs  arbitarly fixed up to 20
  int fluidity[4][20];		//older value  
  int new_fluidity[4][20];	//newer value
  int has_input[5];
  int has_input_vc[5][20];

  Router( const Configuration& config,
	  Module *parent, const string & name, int id,
	  int inputs, int outputs );

  virtual ~Router( );

  static Router *NewRouter( const Configuration& config,
			    Module *parent, string name, int id,
			    int inputs, int outputs );

  void AddInputChannel( FlitChannel *channel, CreditChannel *backchannel );
  void AddOutputChannel( FlitChannel *channel, CreditChannel *backchannel );
 
   vector<Router *> *_neighbours;//added by KVM

  virtual void ReadInputs( ) = 0;
  virtual void InternalStep( ) = 0;
  virtual void WriteOutputs( ) = 0;

  void OutChannelFault( int c, bool fault = true );
  bool IsFaultyOutput( int c ) const;

  int GetID( ) const;
  
  //added by KVM
  void IncrementNOF();
  int GetNOF();
  
  void IncrementNOF_port(int port);
  int GetNOF_port(int port);
  
  int GetNOF_mean(vector<int> neighbour_ports);
  int* GetNOF_weighted_neighbours();
  int* GetNOF_neighbours();
  void calc_wnof_all_ports();
  void update_wnof_values();
  int* get_wnof_old();
  
  void Increment_present_flits(int port);
  void Update_cum_flits();
  int GetNOF_weighted(vector<int> neighbour_ports);
  
  void update_nop_values();
  int* get_free_vcs_old();
   //shankar
  int* get_inport_util();
  int* get_outport_util();
  int *get_in_time();
  int* get_out_time();
  void Clear_cum_in_time();
  void Clear_cum_out_time();
  void Update_cum_time_out();
  void Clear_has_input();//fluidity
  void Update_fluidity();
  list<int> * get_time_in_bits();
  
  
  virtual void free_vcs_all_ports(int*)
  {
  //cout<<"in router"<<endl;
  }


  virtual int GetCredit(int out, int vc_begin, int vc_end ) const = 0;
  virtual int GetBuffer(int i = -1) const = 0;
  virtual int GetReceivedFlits(int i = -1) const = 0;
  virtual int GetSentFlits(int i = -1) const = 0;
  virtual void ResetFlitStats() = 0;

  int NumOutputs(){return _outputs;}
};

#endif
