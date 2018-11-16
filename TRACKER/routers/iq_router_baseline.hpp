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

#ifndef _IQ_ROUTER_BASELINE_HPP_
#define _IQ_ROUTER_BASELINE_HPP_

#include <string>

#include "module.hpp"
#include "iq_router_base.hpp"

class Allocator;

class IQRouterBaseline : public IQRouterBase {

private:

  int  _speculative ;
  int  _filter_spec_grants ;
  
  Allocator *_vc_allocator;
  Allocator *_sw_allocator;
  Allocator *_spec_sw_allocator;
  
  vector<int> _sw_rr_offset;

protected:

  void _VCAlloc( );
  void _SWAlloc( );
  int set_pri_stress(list<OutputSet::sSetElement>::const_iterator);
  virtual void _Alloc( );
  
public:

  IQRouterBaseline( const Configuration& config,
	    Module *parent, const string & name, int id,
	    int inputs, int outputs );
  
  virtual ~IQRouterBaseline( );
  //vector<Router *> *_neighbours;//added by KVM
 void free_vcs_all_ports(int* fv)
{
	BufferState *dest_vc;
	//fv=new int[4];
	for(int p=0;p<4;p++)
	{
	 	fv[p]=0;
	 	dest_vc= _next_vcs[p];
		for(int out_vc =0; out_vc <_vcs; ++out_vc )
		{		
			if(dest_vc->IsAvailableFor(out_vc))
				fv[p]++;
		}
	}
}

  int nop(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset);
  int set_pri_num_flits_ports_weighted(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc); //TRACKER
  int set_pri_num_flits_ports_vcs(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc);
  void oddeven_modified_paths( int cur, const Flit *f, int in_channel, vector<int>* paths );
 // ****
  int time_spent_out_router(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc); // BOFAR
  int set_pri_by_fluidity(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc);
  int bofar_tracker_comp(Flit* f,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc);
  
};

#endif
