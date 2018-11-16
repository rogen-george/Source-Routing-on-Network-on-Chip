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

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <assert.h>
#include<fstream>

#include "globals.hpp"
#include "random_utils.hpp"
#include "misc_utils.hpp"
#include "network.hpp"
#include "vc.hpp"
#include "routefunc.hpp"
#include "outputset.hpp"
#include "buffer_state.hpp"
#include "pipefifo.hpp"
#include "allocator.hpp"
#include "iq_router_baseline.hpp"

IQRouterBaseline::IQRouterBaseline( const Configuration& config,
				    Module *parent, const string & name, int id,
				    int inputs, int outputs )
  : IQRouterBase( config, parent, name, id, inputs, outputs )
{
  //cout<<"iq"<<endl;
  string alloc_type;
  string arb_type;
  int iters;

  // Alloc allocators
  config.GetStr( "vc_allocator", alloc_type );
  config.GetStr( "vc_alloc_arb_type", arb_type );
  iters = config.GetInt( "vc_alloc_iters" );
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _vc_allocator = Allocator::NewAllocator( this, "vc_allocator",
					   alloc_type,
					   _vcs*_inputs,
					   _vcs*_outputs,
					   iters, arb_type );

  if ( !_vc_allocator ) {
    cout << "ERROR: Unknown vc_allocator type " << alloc_type << endl;
    exit(-1);
  }

  config.GetStr( "sw_allocator", alloc_type );
  config.GetStr( "sw_alloc_arb_type", arb_type );
  iters = config.GetInt("sw_alloc_iters");
  if(iters == 0) iters = config.GetInt("alloc_iters");
  _sw_allocator = Allocator::NewAllocator( this, "sw_allocator",
					   alloc_type,
					   _inputs*_input_speedup, 
					   _outputs*_output_speedup,
					   iters, arb_type );

  if ( !_sw_allocator ) {
    cout << "ERROR: Unknown sw_allocator type " << alloc_type << endl;
    exit(-1);
  }
  
  _speculative = config.GetInt( "speculative" ) ;
  
  if ( _speculative >= 2 ) {
    
    string filter_spec_grants;
    config.GetStr("filter_spec_grants", filter_spec_grants);
    if(filter_spec_grants == "any_nonspec_gnts") {
      _filter_spec_grants = 0;
    } else if(filter_spec_grants == "confl_nonspec_reqs") {
      _filter_spec_grants = 1;
    } else if(filter_spec_grants == "confl_nonspec_gnts") {
      _filter_spec_grants = 2;
    } else assert(false);
    
    _spec_sw_allocator = Allocator::NewAllocator( this, "spec_sw_allocator",
						  alloc_type,
						  _inputs*_input_speedup, 
						  _outputs*_output_speedup,
						  iters, arb_type );
    if ( !_spec_sw_allocator ) {
      cout << "ERROR: Unknown sw_allocator type " << alloc_type << endl;
      exit(-1);
    }

  }

  _sw_rr_offset.resize(_inputs*_input_speedup);
  //cout<<"eiq"<<endl;
}

IQRouterBaseline::~IQRouterBaseline( )
{
  delete _vc_allocator;
  delete _sw_allocator;

  if ( _speculative >= 2 )
    delete _spec_sw_allocator;
    
}
  
void IQRouterBaseline::_Alloc( )//for a particular router
{
  
  _VCAlloc( );
  _SWAlloc( );
}
//priority is set based on the number of free vcs available at the port.
//if the number of free vcs is less than half of total vcs at that port, then the priority is set as 0(least)
//otherwise the priority remains the same as set by the routing function
int set_pri_free_vcs(BufferState *dest_vc,list<OutputSet::sSetElement>::const_iterator iset,int num_vcs)//added by KVM
{
	int free_vcs=0;
	for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc )
	{	
		if(dest_vc->IsAvailableFor(out_vc))
			free_vcs++;
	}
	/*if(free_vcs<(num_vcs/2))
		return 0;
	else
		return iset->pri;*/
	return free_vcs;
}
//this is wrong.have to be modified 
int IQRouterBaseline::set_pri_stress(list<OutputSet::sSetElement>::const_iterator iset)//added by KVM
{  
	int busy_vcs=0;
	BufferState *dest_vc;
	for(int p=0;p<4;p++)
	{
	 	dest_vc= _next_vcs[p];
	
		for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc )
		{		
			if(!dest_vc->IsAvailableFor(out_vc))
				busy_vcs++;
		}
	}
	return -busy_vcs;
}

//priority is set based on the number of flits that have already passed through the router.
//a router through which less flits have passed has higher priority and vice versa
int set_pri_num_flits(Router* router,list<OutputSet::sSetElement>::const_iterator iset)
{
	//cout<<"in set_pri_num_flits";
	//cout<<"router:"<<router->GetID()<<endl;
	//cout<<"outport:"<<iset->output_port<<endl;
	Router* next_router;
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		next_router=(router->_neighbours)->at(iset->output_port);
		//cout<<"next router is:"<<next_router->GetID()<<endl;
		//cout<<"returned 2"<<endl;
		return -(next_router->GetNOF());
	}
	
	
}
int set_pri_num_flits_ports(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset)
{
	Router* next_router;
	int dest,cur,next,destx,desty,curx,cury,nextx,nexty;
	int sum_flits=0,mean_flits=0;
	vector<int> neighbour_ports;
	vector<int>::iterator p;
	dest=f->dest;
	cur=router->GetID();
	destx=dest%gK;
  	desty=dest/gK;
  	curx=cur%gK;
  	cury=cur/gK;
  	if(f->id==28760)
  	{
  		fp<<"current router:"<<cur<<endl;
  	}
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		next_router=(router->_neighbours)->at(iset->output_port);
		
		next=next_router->GetID();
		if(f->id==28760)
  		{
  			fp<<"next router:"<<next<<endl;
  		}
		nextx=next%gK;
		nexty=next/gK;
		if(nextx==destx && nexty==desty)
			return iset->pri;
		if(nextx<destx)
		{
			neighbour_ports.push_back(0);
		}
		else if(nextx>destx)
		{
			neighbour_ports.push_back(1);
		}
		if(nexty<desty)
		{
			neighbour_ports.push_back(2);
		}
		else if(nexty>desty)
		{
			neighbour_ports.push_back(3);
		}		
		mean_flits=next_router->GetNOF_mean(neighbour_ports);
		
		return -mean_flits;
	}
}


/*int set_pri_num_flits_ports_vcs(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
	Router* next_router;
	int dest,cur,next,destx,desty,curx,cury,nextx,nexty;
	int sum_flits=0,mean_flits=0;
	vector<int> neighbour_ports;
	vector<int>::iterator p;
	dest=f->dest;
	cur=router->GetID();
	destx=dest%gK;
  	desty=dest/gK;
  	curx=cur%gK;
  	cury=cur/gK;
  	if(f->id==28760)
  	{
  		fp<<"current router:"<<cur<<endl;
  	}
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		next_router=(router->_neighbours)->at(iset->output_port);
		
		next=next_router->GetID();
		if(f->id==28760)
  		{
  			fp<<"next router:"<<next<<endl;
  		}
		nextx=next%gK;
		nexty=next/gK;
		if(nextx==destx && nexty==desty)
			return iset->pri;
		if(nextx<destx)
		{
			neighbour_ports.push_back(0);
		}
		else if(nextx>destx)
		{
			neighbour_ports.push_back(1);
		}
		if(nexty<desty)
		{
			neighbour_ports.push_back(2);
		}
		else if(nexty>desty)
		{
			neighbour_ports.push_back(3);
		}		
		
		
		mean_flits=next_router->GetNOF_mean(neighbour_ports);
		
		//cout<<"next router is:"<<next_router->GetID()<<endl;
		//cout<<"returned 2"<<endl;	
	}
	int free_vcs=0;
	for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc )
	{	
		if(dest_vc->IsAvailableFor(out_vc))
			free_vcs++;
	}
	if(free_vcs!=0)
		return -(mean_flits/(free_vcs*free_vcs));
	else
		return -mean_flits;
	
}*/
int IQRouterBaseline::set_pri_num_flits_ports_vcs(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
	Router* next_router;
	int dest,cur,next,in_channel;
	int sum_flits=0,mean_flits=0;
	int* NOF;
	vector<int> paths; 
	vector<int>::iterator p;
	dest=f->dest;
	cur=router->GetID();
	
  	if(f->id==176)
  	{
  		fp<<"current router:"<<cur<<endl;
  	}
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		next_router=(router->_neighbours)->at(iset->output_port);
		next=next_router->GetID();
		if(next==f->dest)
			return iset->pri;
		NOF=next_router->GetNOF_neighbours();
		switch(iset->output_port)
		{
			case 0:
				in_channel=1;
				break;
			case 1:
				in_channel=0;
				break;
			case 2:
				in_channel=3;
				break;
			case 3:
				in_channel=2;
				break;
		};
		
		if(f->id==176)
  		{
  			fp<<"next router:"<<next<<endl;
  			
  		}
  		oddeven_modified_paths( next, f,in_channel,&paths );
  		for(p=paths.begin();p<paths.end();p++)
  		{
  			if(f->id==176)
  			{
  				fp<<"p is"<<*p<<endl;
  				fp<<"nof is"<<NOF[(*p)];
  			}
  			sum_flits=sum_flits+NOF[(*p)];	
		}
		mean_flits=sum_flits/paths.size();
		
		//cout<<"next router is:"<<next_router->GetID()<<endl;
		//cout<<"returned 2"<<endl;	
	}
	int free_vcs=0;
	for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc )
	{	
		if(dest_vc->IsAvailableFor(out_vc))
			free_vcs++;
	}
	if(free_vcs!=0)
		return -(mean_flits/(free_vcs));
	else
		return -mean_flits;
	
}
void IQRouterBaseline::oddeven_modified_paths( int cur, const Flit *f, int in_channel, vector<int>* paths )
{
  //cout<<"odd-even"<<endl;
  //bool print_coords=false;
  int out_port,dest,src;
  int s0,s1,d0,d1,c0,c1,e0,e1;
  //cout<<"in-channel is"<<in_channel<<endl;
  //getchar();
  dest=f->dest;
  src=f->src;
  s0=src%gK;
  s1=src/gK;
  d0=dest%gK;
  d1=dest/gK;
  c0=cur%gK;
  c1=cur/gK;
// if(f->id>=0)//added by KVM
  //{
  //print_coords=true;
  //cout<<"in dor_mesh"<<endl;
  //fp<<(*f);
  //getchar();
  //}
  
  e0=d0-c0;
  e1=d1-c1;
  
  if(e0==0 && e1==0)
  {
  	paths->push_back( 4); //deliver the packet to the local node and exit
  }
  else if(e0==0)//currently in the same column as dest
  {
  	if(e1>0)
  	{
  		if(in_channel!=2)
  			paths->push_back(2);;//add north
  	}
  	else
  	{
  		if(in_channel!=3)
  			paths->push_back( 3);//add south
  	}
  }
  else
  {
  	if(e0>0) //east-bound messages
  	{
  		if(e1==0)//currently in the same row as destination
  		{
  			if(in_channel!=0)
  				paths->push_back( 0);//add east
  		}
  		else
  		{
  			if((c0%2==1) || (c0==s0))
  			{
  				if(e1>0)
  				{
  					if(in_channel!=2)
  						paths->push_back( 2);//add north
  				}
  				else
  				{
  					if(in_channel!=3)
  						paths->push_back( 3);//add south
  				}
  			}
  			if((d0%2==1) || (e0!=1))//odd dest column or >= 2 columns to dest
  			{
  				if(in_channel!=0)
  					paths->push_back( 0);//add east
  			}
  		}
  	}
  	else //west-bound messages
  	{
  		if(in_channel!=1)
  			paths->push_back( 1);//add west
  		if(c0%2==0)
  		{
  			if(e1>0)
  			{
  				if(in_channel!=2)
  					paths->push_back( 2);//add north
  			}
  			else if(e1<0)
  			{
  				if(in_channel!=3)
  					paths->push_back( 3);//add south
  			}
  		}
  	}
  }  
  
}
//old function
/*int IQRouterBaseline::set_pri_num_flits_ports_weighted(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
	Router* next_router;
	int dest,cur,next,destx,desty,curx,cury,nextx,nexty;
	int sum_flits=0,mean_flits=0;
	vector<int> neighbour_ports;
	vector<int>::iterator p;
	dest=f->dest;
	cur=router->GetID();
	destx=dest%gK;
  	desty=dest/gK;
  	curx=cur%gK;
  	cury=cur/gK;
  	if(f->id==28760)
  	{
  		fp<<"current router:"<<cur<<endl;
  	}
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		next_router=(router->_neighbours)->at(iset->output_port);
		
		next=next_router->GetID();
		if(f->id==28760)
  		{
  			fp<<"next router:"<<next<<endl;
  		}
		nextx=next%gK;
		nexty=next/gK;
		if(nextx==destx && nexty==desty)
			return iset->pri;
		if(nextx<destx)
		{
			neighbour_ports.push_back(0);
		}
		else if(nextx>destx)
		{
			neighbour_ports.push_back(1);
		}
		if(nexty<desty)
		{
			neighbour_ports.push_back(2);
		}
		else if(nexty>desty)
		{
			neighbour_ports.push_back(3);
		}		
		
		
		mean_flits=next_router->GetNOF_weighted(neighbour_ports);
		
		//cout<<"next router is:"<<next_router->GetID()<<endl;
		//cout<<"returned 2"<<endl;	
	}
	/*int free_vcs=0;
	float k=0.5;
	for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc )
	{	
		if(dest_vc->IsAvailableFor(out_vc))
			free_vcs++;
	}
	if(free_vcs!=0)
		return -(mean_flits/(k*free_vcs));
	else*/
		/*return -mean_flits;
	
}*/
//new function  (TRACKER)
int IQRouterBaseline::set_pri_num_flits_ports_weighted(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
	Router* next_router;
	int dest,cur,next,in_channel;
	int sum_flits=0,mean_flits=0;
	int* NOF_weighted;
	vector<int> paths; 
	vector<int>::iterator p;
	dest=f->dest;
	cur=router->GetID();
	
  	if(f->id==176)
  	{
  		fp<<"current router:"<<cur<<endl;
  	}
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		next_router=(router->_neighbours)->at(iset->output_port);
		next=next_router->GetID();
		if(next==f->dest)
			return iset->pri;
		NOF_weighted=next_router->get_wnof_old();
		//free_vcs=next_router->get_free_vcs_old();//now added
		switch(iset->output_port)
		{
			case 0:
				in_channel=1;
				break;
			case 1:
				in_channel=0;
				break;
			case 2:
				in_channel=3;
				break;
			case 3:
				in_channel=2;
				break;
		};
		
		if(f->id==176)
  		{
  			fp<<"next router:"<<next<<endl;
  			
  		}
  		oddeven_modified_paths( next, f,in_channel,&paths );
  		for(p=paths.begin();p<paths.end();p++)
  		{
  			if(f->id==176)
  			{
  				fp<<"p is"<<*p<<endl;
  				fp<<"nof is"<<NOF_weighted[(*p)];
  			}
  			sum_flits=sum_flits+NOF_weighted[(*p)];	
		}
		mean_flits=sum_flits/paths.size();
		
		//cout<<"next router is:"<<next_router->GetID()<<endl;
		//cout<<"returned 2"<<endl;	
	}
	/*int free_vcs=0;
	float k=0.5;
	for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc )
	{	
		if(dest_vc->IsAvailableFor(out_vc))
			free_vcs++;
	}
	if(free_vcs!=0)
		return -(mean_flits/(k*free_vcs));
	else*/
		return -mean_flits;
	
}
// Fluidity of Neighbors (FON)
int IQRouterBaseline::set_pri_by_fluidity(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
	Router* next_router;
	Router* next_next;
	int dest,cur,next,in_channel;
	vector<int> paths;
	vector<int>::iterator p;
	dest=f->dest;
	int fluidity=0;
	cur=router->GetID();
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		next_router=(router->_neighbours)->at(iset->output_port);
		next=next_router->GetID();
		if(next==f->dest)
			return iset->pri;
		switch(iset->output_port)
		{
			case 0:
				in_channel=1;
				break;
			case 1:
				in_channel=0;
				break;
			case 2:
				in_channel=3;
				break;
			case 3:
				in_channel=2;
				break;
		};

		oddeven_modified_paths( next, f,in_channel,&paths );
		int i=0;
		//cout<<"\n";
  		for(p=paths.begin();p<paths.end();p++)
  		{
  			next_next=(next_router->_neighbours)->at(*p);
  			switch(*p)
  			{
  				case 0:	in_channel=1;	break;
  				case 1: in_channel=0;	break;
  				case 2:	in_channel=3;	break;
  				case 3:	in_channel=2;	break;
  			};
  			//f1[i]=(f1[i++])  & ((int)(next_next->fluidity[in_channel].x));
  			for(int k=0;k<4;k++)
  				fluidity=fluidity+next_next->fluidity[in_channel][k];
  			//cout<<"\t in fluidity   "<<f1[i]<<"    in_channel="<<in_channel;
  			
  		}
		//fluidity=no of ones in f1[0]+ no of ones in f1[1];
		//cout<<"\t fluidi="<<fluidity;
	}
	//cout<<cur<< "  " << fluidity<<endl;
	return fluidity;

}
// BOFAR
int IQRouterBaseline::time_spent_out_router(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
				Router* next_router;
				int dest,cur,next,in_channel,temp;
				int total_time=0,mean_time=0;
				int* time_from_neig;
				//int* port_util;
				vector<int> paths;
				vector<int>::iterator p;
				dest=f->dest;
				cur=router->GetID();
			  	if(f->id==200)
			  	{
					fp<<"src"<<f->src<<"dest"<<f->dest<<endl;
			  		fp<<"current router:"<<cur<<endl;
			  	}
				if(iset->output_port==4)
				{
					//cout<<"returned 1"<<endl;
					return iset->pri;
				}
				else
				{
					next_router=(router->_neighbours)->at(iset->output_port);
					next=next_router->GetID();
					if(next==f->dest)
						return iset->pri;
					//NOF_weighted=next_router->get_wnof_old();
					//free_vcs=next_router->get_free_vcs_old();//now added
					time_from_neig=next_router->get_out_time();
					//port_util=next_router->get_outport_util();
					switch(iset->output_port)
					{
						case 0:
							in_channel=1;
							break;
						case 1:
							in_channel=0;
							break;
						case 2:
							in_channel=3;
							break;
						case 3:
							in_channel=2;
							break;
					};

					if(f->id==200)
			  		{
			  			fp<<"next router:"<<next<<endl;

			  		}
			  		oddeven_modified_paths( next, f,in_channel,&paths );
			  		for(p=paths.begin();p<paths.end();p++)
			  		{
			  			//	total_time=total_time+time_from_neig[(*p)];
			  			total_time=total_time+(time_from_neig[(*p)]);
					}
					mean_time=total_time/paths.size();
				}
						return -mean_time;


}

/*
wrong one (iccad initial)
int IQRouterBaseline::nop(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset)
{
	//cout<<"nop"<<endl;
	Router* next_router;
	int dest,cur,next,in_channel;
	int sum_vcs=0,mean_vcs=0;
	int *free_vcs;
	//IQRouterBaseline *next_iqbl;
	vector<int> paths; 
	vector<int>::iterator p;
	dest=f->dest;
	cur=router->GetID();
	
  	if(f->id==176)
  	{
  		fp<<"current router:"<<cur<<endl;
  	}
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		//cout<<"in else"<<endl;
		next_router=(router->_neighbours)->at(iset->output_port);
		next=next_router->GetID();
		//next_iqbl=next_router;
		if(next==f->dest)
			return iset->pri;
		//free_vcs=next_router->get_free_vcs_old();
		switch(iset->output_port)
		{
			case 0:
				in_channel=1;
				break;
			case 1:
				in_channel=0;
				break;
			case 2:
				in_channel=3;
				break;
			case 3:
				in_channel=2;
				break;
		};
		
		if(f->id==176)
  		{
  			fp<<"next router:"<<next<<endl;
  			
  		}
  		oddeven_modified_paths( next, f,in_channel,&paths );
  		Router* next_next;
  		for(p=paths.begin();p<paths.end();p++)
  		{
  			next_next=(next_router->_neighbours)->at(*p);
  			free_vcs=next_next->get_free_vcs_old();
  			switch(iset->output_port)
			{
				case 0:
					in_channel=1;
					break;
				case 1:
					in_channel=0;
					break;
				case 2:
					in_channel=3;
					break;
				case 3:
					in_channel=2;
					break;
			};
  			
  			if(f->id==176)
  			{
  				fp<<"p is"<<*p<<endl;
  				fp<<"fv is"<<free_vcs[(*p)];
  			}
  			//cout<<"p is"<<*p<<endl;
  			//cout<<"nof is"<<free_vcs[(*p)]<<endl;
  			sum_vcs=sum_vcs+free_vcs[in_channel];	
		}

		
		//cout<<"next router is:"<<next_router->GetID()<<endl;
		//cout<<"returned 2"<<endl;	
	}
	
	//return sum_vcs/paths.size();   //  to return mean fvcs at 2 hop neighbor
	//cout<<cur<< "  " << sum_vcs<<endl;
	 return sum_vcs; 		 // to return sum of all fvcs at 2 hop neighbor
	
}
*/
//// this is correct.. see below
int IQRouterBaseline::nop(Flit* f, Router* router,list<OutputSet::sSetElement>::const_iterator iset)
{
	//cout<<"nop"<<endl;
	Router* next_router;
	int dest,cur,next,in_channel;
	int sum_vcs=0,mean_vcs=0;
	int *free_vcs;
	//IQRouterBaseline *next_iqbl;
	vector<int> paths; 
	vector<int>::iterator p;
	dest=f->dest;
	cur=router->GetID();
	
  	if(f->id==200)
  	{
  		fp<<"current router:"<<cur<<endl;
  	}
	if(iset->output_port==4)
	{
		//cout<<"returned 1"<<endl;
		return iset->pri;
	}
	else
	{
		//cout<<"in else"<<endl;
		next_router=(router->_neighbours)->at(iset->output_port);
		next=next_router->GetID();
		//next_iqbl=next_router;
		if(next==f->dest)
			return iset->pri;
		free_vcs=next_router->get_free_vcs_old();
		switch(iset->output_port)
		{
			case 0:
				in_channel=1;
				break;
			case 1:
				in_channel=0;
				break;
			case 2:
				in_channel=3;
				break;
			case 3:
				in_channel=2;
				break;
		};
		
		if(f->id==200)
  		{
  			fp<<"next router:"<<next<<endl;
  			
  		}
  		oddeven_modified_paths( next, f,in_channel,&paths );
  		for(p=paths.begin();p<paths.end();p++)
  		{
  			if(f->id==200)
  			{
  				fp<<"p is"<<*p<<endl;
  				fp<<"fv is"<<free_vcs[(*p)];
  			}
  			//cout<<"p is"<<*p<<endl;
  			//cout<<"nof is"<<free_vcs[(*p)]<<endl;
  			//sum_vcs=sum_vcs+free_vcs[(*p)];	 //use one of this
			sum_vcs=sum_vcs+free_vcs[(in_channel)];	
		}

		
		//cout<<"next router is:"<<next_router->GetID()<<endl;
		//cout<<"returned 2"<<endl;	
	}
	
	return sum_vcs;
	
}




//.. nop ends here
int set_pri_flits_through_channel(Router* router,list<OutputSet::sSetElement>::const_iterator iset)
{
	int flits;
	flits=router->GetNOF_port(iset->output_port);
	return -flits;
}

// tracker&BOFAR together

int IQRouterBaseline::bofar_tracker_comp(Flit* f,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
	static int prev_bofar=-1;
	static int prev_tracker=-1;
        static int prev_pri=-1;
        static int prev_fid=-1;
        int in_priority=0;
        int present_bofar,present_tracker;
	 
	if(prev_fid==f->id)
	{
	  	//cout<<"if"<<endl;
	  	present_bofar=time_spent_out_router(f, this,iset,dest_vc);
	  	present_tracker=set_pri_num_flits_ports_weighted(f, this,iset,dest_vc);
		// larger better because of negative value

		/***** Type -1  Check T if same check B if del(B) is within 8 check
		if(prev_tracker>present_tracker)  // previous T better
		{
			if(prev_bofar>=present_bofar) // previous B better
	  		{
	  			in_priority=prev_pri-1;  // previous better in both T & B, so reduce priority of present one
	  		}
	  		else // ( previous T better , present B better )
	  		{
	  		  	if(abs(prev_bofar-present_bofar)<8)   
					in_priority=prev_pri-1;  // present B only slightly better so reduce priority of present one 
				else 
					in_priority=prev_pri+1;  //  // present B much better so increase priority of present one 
	  		}
		}
		else if(prev_tracker<present_tracker) // present T better
		{
			if(prev_bofar<=present_bofar) // // present B better
	  		{
	  			in_priority=prev_pri+1; // present better in both T & B, so increase priority of present one
	  		}
	  		else // ( present T better ,  previous B better )
	  		{
	  		  	if(abs(prev_bofar-present_bofar)<8)   
					in_priority=prev_pri+1;  // previous B only slightly better so increase priority of present one 
				else 
					in_priority=prev_pri-1;  //  //previous B much better so reduce priority of present one 
	  		}
		}
		else // T is same for previous and present
		{
			if(prev_bofar>=present_bofar) // previous B better
	  			in_priority=prev_pri-1;  // So reduce priority of present one
	  		else
				in_priority=prev_pri+1;  // So increase priority of present one
		}

		*/

		//  ******  type 2  if del (BOF) <16 and del (TRA) > 4 --> tracker else bofar
		
		if(abs(prev_bofar>present_bofar)<16)
	  	{
	  		if(abs(prev_tracker-present_tracker)<=4)  // small variation in t so look for B
	  		{
	  			if(prev_bofar<=present_bofar) // // present B better
	  				in_priority=prev_pri+1; // present better in both T & B, so increase priority of present one
	  			else
					in_priority=prev_pri-1;  // past B better
	  		}
	  		else  // large variation in T, so look for T values
	  		{
	  		  	if(prev_tracker<=present_tracker)
					in_priority=prev_pri+1; // present T better
	  			else
					in_priority=prev_pri-1; // past T better
	  		}
	 	}
		else  // variation in B is more than 16, so look for B
		{	
			if(prev_bofar<=present_bofar) // // present B better
	  			in_priority=prev_pri+1; // present better in both T & B, so increase priority of present one
	  		else
				in_priority=prev_pri-1;  // past B better
		}
		// 	 alternate T and B coding
		/*
	  	if(abs(prev_tracker-present_tracker)<4)
	  	{
	  		if(prev_bofar>present_bofar)
	  		{
	  			in_priority=prev_pri-1;
	  		}
	  		else
	  		{
	  		  	in_priority=prev_pri+1;
	  		}
	 	}
	  	else
	  	{
	  		if(prev_tracker>present_tracker)
	  		{
	  			in_priority=prev_pri-1;
	  		}
	  		else
	  		{
	  			in_priority=prev_pri+1;
	  		}
	  		
	  	}*/
	  	prev_bofar=-1;
	  	prev_tracker=-1;
	  	prev_pri=-1;
	}
	else
	{
	  	//cout<<"else"<<endl;
	  	in_priority=iset->pri;
	  	prev_bofar=time_spent_out_router(f, this,iset,dest_vc);
	  	prev_tracker=set_pri_num_flits_ports_weighted(f, this,iset,dest_vc);
	  	prev_pri=in_priority;	
	}
	prev_fid=f->id;
	return in_priority;
}


int free_vcs_comp(Flit* f,list<OutputSet::sSetElement>::const_iterator iset,BufferState *dest_vc)
{
	static int prev_fv=-1;
        static int prev_pri=-1;
        static int prev_fid=-1;
        int in_priority=0;
	int free_vcs_count=0;
	for(int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc )
	{	
		if(dest_vc->IsAvailableFor(out_vc))
			free_vcs_count++;
	}
	 
	if(prev_fid==f->id)
	{
	  	//cout<<"if"<<endl;
	  	if(prev_fv<free_vcs_count)
	  	{
	  		in_priority=prev_pri+1;
	 	}
	  	else if(prev_fv>free_vcs_count)
	  	{	
	  		in_priority=prev_pri-1;
	  	}
	  	else
	  	{
	  		in_priority=prev_pri;
	  	}
	  	prev_fv=-1;
	  	prev_pri=-1;
	  	//prev_fv=free_vcs_count;
	  	//prev_pri=in_priority;
	}
	else
	{
	  	//cout<<"else"<<endl;
	  	in_priority=iset->pri;
		prev_fv=free_vcs_count;
	  	prev_pri=in_priority;	
	}
	prev_fid=f->id;
	return in_priority;
}
int flits_comp(Flit* f,list<OutputSet::sSetElement>::const_iterator iset,Router* router)
{
	static int ne=0,nw=0,se=0,sw=0;
	static int prev_port=-1;
	int in_priority,flits;
	static int prev_flits,prev_pri;
	static int prev_fid=-1;
	flits=router->GetNOF_port(iset->output_port);
	
	if(prev_fid==f->id)
	{
		if(prev_flits-flits<10)
		{
			if(prev_flits<flits)
	  		{
	  			in_priority=prev_pri-1;
	  		}
	  		else if(prev_flits>flits)
	  		{	
	  			in_priority=prev_pri+1;
	  		}
	  		else
	  		{
	  			in_priority=prev_pri;
	  		}
		}
		else
		{
		  if(iset->output_port==0)//east
		  {
			if(prev_port==2)
			{
				if(ne==0)
				{
					in_priority=prev_pri+1;
					ne=1;
				}
				else if(ne==1)
				{
					in_priority=prev_pri-1;
					ne=0;
				}
			}
			if(prev_port==3)
			{
				if(se==0)
				{
					in_priority=prev_pri+1;
					se=1;
				}
				else if(se==1)
				{
					in_priority=prev_pri-1;
					se=0;
				}
			}
		
		}
		else if(iset->output_port==1)//west
		{
			if(prev_port==2)
			{
				if(nw==0)
				{
					in_priority=prev_pri+1;
					nw=1;
				}
				else if(nw==1)
				{
					in_priority=prev_pri-1;
					nw=0;
				}
			}
			if(prev_port==3)
			{
				if(sw==0)
				{
					in_priority=prev_pri+1;
					sw=1;
				}
				else if(sw==1)
				{
					in_priority=prev_pri-1;
					sw=0;
				}
			}
		}
		else if(iset->output_port==2)//north
		{
			if(prev_port==0)
			{
				if(ne==0)
				{
					in_priority=prev_pri-1;
					ne=1;
				}
				else if(ne==1)
				{
					in_priority=prev_pri+1;
					ne=0;
				}
			}
			if(prev_port==1)
			{
				if(nw==0)
				{
					in_priority=prev_pri-1;
					nw=1;
				}
				else if(nw==1)
				{
					in_priority=prev_pri+1;
					nw=0;
				}
			}
		}
		else if(iset->output_port==3)//south
		{
			if(prev_port==0)
			{
				if(se==0)
				{
					in_priority=prev_pri-1;
					se=1;
				}
				else if(se==1)
				{
					in_priority=prev_pri+1;
					se=0;
				}
			}
			if(prev_port==1)
			{
				if(sw==0)
				{
					in_priority=prev_pri-1;
					sw=1;
				}
				else if(sw==1)
				{
					in_priority=prev_pri+1;
					sw=0;
				}
			}
		}
		else
		{
			in_priority=iset->pri;
		}
             }
             prev_port=-1;
	     prev_flits=0;
	     prev_pri=0;
	}
	else
	{
		
		in_priority=iset->pri;
		prev_port=iset->output_port;
		prev_flits=flits;
		prev_pri=in_priority;
	}
	prev_fid=f->id;
	return in_priority;
}

void IQRouterBaseline::_VCAlloc( )//for a particular router
{
  //cout<<"alloc"<<this->GetID()<<endl;
  VC          *cur_vc;
  BufferState *dest_vc;
  int         input_and_vc;
  int         match_input;
  int         match_vc;

  Flit        *f;
  bool        watched = false;

  _vc_allocator->Clear( );


  for ( set<int>::iterator item = _vcalloc_vcs.begin(); item!=_vcalloc_vcs.end(); ++item ) {
    int vc_encode = *item;
    int input =  vc_encode/_vcs;
    int vc =vc_encode%_vcs;
    cur_vc = _vc[input][vc];
    if ( ( _speculative > 0 ) && ( cur_vc->GetState( ) == VC::vc_alloc )){
      cur_vc->SetState( VC::vc_spec ) ;
    }
    if (  cur_vc->GetStateTime( ) >= _vc_alloc_delay  ) {
      f = cur_vc->FrontFlit( );
      if(f->id==28760)//added by KVM
      {
      fp<< GetSimTime() << " | " << FullName() << " | " 
		   << "VC " << vc << " at input " << input
		   << " is requesting VC allocation for flit " << f->id
		   << "." << endl;
		   //getchar();
      }
      if(f->watch) {
	*gWatchOut << GetSimTime() << " | " << FullName() << " | " 
		   << "VC " << vc << " at input " << input
		   << " is requesting VC allocation for flit " << f->id
		   << "." << endl;
	watched = true;
      }
      //this is all for a single flit
      const OutputSet *route_set    = cur_vc->GetRouteSet( );
      //OutputSet *route_set    = cur_vc->GetRouteSet( );
      
      int out_priority = cur_vc->GetPriority( );
      const list<OutputSet::sSetElement>* setlist = route_set ->GetSetList();//GetSetList() is in outputset.cpp;it returns _outputs which is also for a single flit  
      //list<OutputSet::sSetElement>* setlist = route_set ->GetSetList();
      //cout<<setlist->size()<<endl;
      list<OutputSet::sSetElement>::const_iterator iset = setlist->begin( );
      
      //list<OutputSet::sSetElement>::iterator iset = setlist->begin( );
      int iset_count=0;
      int add_req_count=0;
      
      //added by KVM
      
      //int prev_fv=-1;
      //int prev_pri=0;
      
      int in_priority=0;
      		   
      while(iset!=setlist->end( ))
      {
		//iset_count++;
		BufferState *dest_vc = _next_vcs[iset->output_port];
		
		
		
		//cout<<iset->output_port<<endl<<_next_vcs[iset->output_port]-><<endl;
		//getchar();
		//added by KVM
		
		//************  other suboptimal  selection strategies *********
		//in_priority=free_vcs_comp(f,iset,dest_vc);
		//in_priority=flits_comp(f,iset,this);
		//in_priority=set_pri_stress(iset);
		//in_priority=set_pri_flits_through_channel(this,iset);
		//in_priority=set_pri_num_flits_ports_vcs(f,this,iset,dest_vc);
		//in_priority=set_pri_num_flits_ports(f,this,iset);
				//in_priority=set_pri_num_flits(this,iset);
		//cout<<"pri returned is:"<<in_priority<<endl;

		//************  classical selection strategies ********* (by John )
		//in_priority=set_pri_free_vcs(dest_vc,iset,_vcs); //FVC
		in_priority=set_pri_by_fluidity(f,this,iset,dest_vc);//Fluidity
		//in_priority=nop(f,this,iset); // NOP
		//in_priority=set_pri_num_flits_ports_weighted(f,this,iset,dest_vc); // TRACKER
		//in_priority=time_spent_out_router(f,this,iset,dest_vc);//BOFAR
		
		// *******       combined prirority   ************* //
		//in_priority=bofar_tracker_comp(f,iset,dest_vc);
		
		for ( int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc ) 
		{
			//int in_priority = iset->pri;
			
			// On the input input side, a VC might request several output 
			// VCs.  These VCs can be prioritized by the routing function
			// and this is reflected in "in_priority".  On the output,
			// if multiple VCs are requesting the same output VC, the priority
			// of VCs is based on the actual packet priorities, which is
			// reflected in "out_priority".
	    	
	  		if(dest_vc->IsAvailableFor(out_vc)) //if there is an empty buffer;IsAvailableFor() is in buffer_state.cpp
	  		{
	 
	    			if(f->id==176)//added by KVM
	    			{
	    				fp<< GetSimTime() << " | " << FullName() << " | "
			 		<< "Requesting VC " << out_vc
			 		<< " at output " << iset->output_port 
			 		<< " with priorities " << in_priority
			 		<< " and " << out_priority
			 		<< "." << endl;
			 		//getchar();
	    			}
	    			if(f->watch)
	    			{
	      				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 		<< "Requesting VC " << out_vc
			 		<< " at output " << iset->output_port 
			 		<< " with priorities " << in_priority
			 		<< " and " << out_priority
			 		<< "." << endl;
	    			}
	   			 _vc_allocator->AddRequest(input*_vcs + vc, iset->output_port*_vcs + out_vc, 
				      out_vc, in_priority, out_priority);
				      /*if(f->id==28760)
				      {
				      	_vc_allocator->PrintRequests();
				      	getchar();
				      }*/
				      //add_req_count++;
	  		} 
	  		else 
	  		{
	    			if(f->id==176)
	    			{
	    				fp<< GetSimTime() << " | " << FullName() << " | "
			 		<< "VC " << out_vc << " at output " << iset->output_port 
			 		<< " is unavailable." << endl;
			 		//getchar();
	    			}
	    			if(f->watch)
	      				*gWatchOut << GetSimTime() << " | " << FullName() << " | "
			 		<< "VC " << out_vc << " at output " << iset->output_port 
			 		<< " is unavailable." << endl;
	  		}
		}//end of for
		//go to the next item in the outputset
		iset++;
      }//end of while
      /*cout<<iset_count<<endl;
      cout<<add_req_count<<endl;*/
      //getchar();
    }
    
  }
  //  watched = true;
  if ( watched ) {
    *gWatchOut << GetSimTime() << " | " << _vc_allocator->FullName() << " | ";
    _vc_allocator->PrintRequests( gWatchOut );
  }
//cout<<"in bl"<<endl<<_inputs<<endl<<_outputs<<endl;
  _vc_allocator->Allocate( );//Allocate() present in selalloc.cpp for select allocator
  //cout<<"later"<<endl;
  

  // Winning flits get a VC

  for ( int output = 0; output < _outputs; ++output ) {
    for ( int vc = 0; vc < _vcs; ++vc ) {
      input_and_vc = _vc_allocator->InputAssigned( output*_vcs + vc );

      if ( input_and_vc != -1 ) {
	assert(input_and_vc >= 0);
	match_input = input_and_vc / _vcs;
	match_vc    = input_and_vc - match_input*_vcs;

	cur_vc  = _vc[match_input][match_vc];
	dest_vc = _next_vcs[output];

	if ( _speculative > 0 )
	  cur_vc->SetState( VC::vc_spec_grant );
	else
	  cur_vc->SetState( VC::active );
	_vcalloc_vcs.erase(match_input*_vcs+match_vc);
	
	cur_vc->SetOutput( output, vc );
	dest_vc->TakeBuffer( vc );

	f = cur_vc->FrontFlit( );
	
	
	
	
	
	if(f->id==176)
	{
		fp<<GetSimTime() << " | " << FullName() << " | "
		     << "Granted VC " << vc << " at output " << output
		     << " to VC " << match_vc << " at input " << match_input
		     << " (flit: " << f->id << ")." << endl;
		     //getchar();
	}
	if(f->watch)
	  *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		     << "Granted VC " << vc << " at output " << output
		     << " to VC " << match_vc << " at input " << match_input
		     << " (flit: " << f->id << ")." << endl;
      }
    }
  }
}

void IQRouterBaseline::_SWAlloc( )//for a particular router
{
 //cout<<"in swalloc";
  Flit        *f;
  Credit      *c;

  VC          *cur_vc;
  BufferState *dest_vc;

  int input;
  int output;
  int vc;

  int expanded_input;
  int expanded_output;
  
  bool        watched = false;

  bool any_nonspec_reqs = false;
  bool any_nonspec_output_reqs[_outputs*_output_speedup];
  memset(any_nonspec_output_reqs, 0, _outputs*_output_speedup*sizeof(bool));
  
  _sw_allocator->Clear( );
  if ( _speculative >= 2 )
    _spec_sw_allocator->Clear( );
  
  for ( input = 0; input < _inputs; ++input ) {
    int vc_ready_nonspec = 0;
    int vc_ready_spec = 0;
    for ( int s = 0; s < _input_speedup; ++s ) {
      expanded_input  = s*_inputs + input;
      
      // Arbitrate (round-robin) between multiple 
      // requesting VCs at the same input (handles 
      // the case when multiple VC's are requesting
      // the same output port)
      vc = _sw_rr_offset[ expanded_input ];

      for ( int v = 0; v < _vcs; ++v ) {

	// This continue acounts for the interleaving of 
	// VCs when input speedup is used
	// dub: Essentially, this skips loop iterations corresponding to those 
	// VCs not in the current speedup set. The skipped iterations will be 
	// handled in a different iteration of the enclosing loop over 's'.
	if ( ( vc % _input_speedup ) != s ) {
	  vc = ( vc + 1 ) % _vcs;
	  continue;
	}
	
	cur_vc = _vc[input][vc];

	if(!cur_vc->Empty() &&
	   (cur_vc->GetStateTime() >= _sw_alloc_delay)) {
	  
	  switch(cur_vc->GetState()) {
	    
	  case VC::active:
	    {
	      output = cur_vc->GetOutputPort( );

	      dest_vc = _next_vcs[output];
	      
	      if ( !dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) ) {
		
		// When input_speedup > 1, the virtual channel buffers are 
		// interleaved to create multiple input ports to the switch. 
		// Similarily, the output ports are interleaved based on their 
		// originating input when output_speedup > 1.
		
		assert( expanded_input == (vc%_input_speedup)*_inputs + input );
		expanded_output = 
		  (input%_output_speedup)*_outputs + output;
		
		if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		     ( _switch_hold_out[expanded_output] == -1 ) ) {
		  
		  // We could have requested this same input-output pair in a 
		  // previous iteration; only replace the previous request if 
		  // the current request has a higher priority (this is default 
		  // behavior of the allocators).  Switch allocation priorities 
		  // are strictly determined by the packet priorities.
		  
		  Flit * f = cur_vc->FrontFlit();
		  assert(f);
		  if(f->id==176) {
		    fp << GetSimTime() << " | " << FullName() << " | "
			       << "VC " << vc << " at input " << input 
			       << " requested output " << output 
			       << " (non-spec., exp. input: " << expanded_input
			       << ", exp. output: " << expanded_output
			       << ", flit: " << f->id
			       << ", prio: " << cur_vc->GetPriority()
			       << ")." << endl;
		    
		  }
		  if(f->watch) {
		    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
			       << "VC " << vc << " at input " << input 
			       << " requested output " << output 
			       << " (non-spec., exp. input: " << expanded_input
			       << ", exp. output: " << expanded_output
			       << ", flit: " << f->id
			       << ", prio: " << cur_vc->GetPriority()
			       << ")." << endl;
		    watched = true;
		  }
		  
		  // dub: for the old-style speculation implementation, we 
		  // overload the packet priorities to prioritize 
		  // non-speculative requests over speculative ones
		  if( _speculative == 1 )
		    _sw_allocator->AddRequest(expanded_input, expanded_output, 
					      vc, 1, 1);
		  else
		    _sw_allocator->AddRequest(expanded_input, expanded_output, 
					      vc, cur_vc->GetPriority( ), 
					      cur_vc->GetPriority( ));
		  any_nonspec_reqs = true;
		  any_nonspec_output_reqs[expanded_output] = true;
		  vc_ready_nonspec++;
		}
	      }
	    }
	    break;
	    
	    
	    //
	    // The following models the speculative VC allocation aspects 
	    // of the pipeline. An input VC with a request in for an egress
	    // virtual channel will also speculatively bid for the switch
	    // regardless of whether the VC allocation succeeds. These
	    // speculative requests are handled in a separate allocator so 
	    // as to prevent them from interfering with non-speculative bids
	    //
	  case VC::vc_spec:
	  case VC::vc_spec_grant:
	    {	      
	      assert( _speculative > 0 );
	      assert( expanded_input == (vc%_input_speedup)*_inputs + input );
	      
	      const OutputSet * route_set = cur_vc->GetRouteSet( );
	      const list<OutputSet::sSetElement>* setlist = route_set ->GetSetList();
	      list<OutputSet::sSetElement>::const_iterator iset = setlist->begin( );
	      while(iset!=setlist->end( )){
		BufferState * dest_vc = _next_vcs[iset->output_port];
		bool do_request = false;
		
		// check if any suitable VCs are available
	
		for ( int out_vc = iset->vc_start; out_vc <= iset->vc_end; ++out_vc ) {
		  int vc_prio = iset->pri;
		  if(!do_request && 
		     ((_speculative < 3) || dest_vc->IsAvailableFor(out_vc))) {
		    do_request = true;
		    break;
		  }
		}
		
		if(do_request) { 
		  expanded_output = (input%_output_speedup)*_outputs + iset->output_port;
		  if ( ( _switch_hold_in[expanded_input] == -1 ) && 
		       ( _switch_hold_out[expanded_output] == -1 ) ) {
		    
		    Flit * f = cur_vc->FrontFlit();
		    assert(f);
		    if(f->id==176) {
		      fp << GetSimTime() << " | " << FullName() << " | "
				 << "VC " << vc << " at input " << input 
				 << " requested output " << iset->output_port
				 << " (spec., exp. input: " << expanded_input
				 << ", exp. output: " << expanded_output
				 << ", flit: " << f->id
				 << ", prio: " << cur_vc->GetPriority()
				 << ")." << endl;
		      
		    }
		    if(f->watch) {
		      *gWatchOut << GetSimTime() << " | " << FullName() << " | "
				 << "VC " << vc << " at input " << input 
				 << " requested output " << iset->output_port
				 << " (spec., exp. input: " << expanded_input
				 << ", exp. output: " << expanded_output
				 << ", flit: " << f->id
				 << ", prio: " << cur_vc->GetPriority()
				 << ")." << endl;
		      watched = true;
		    }
		    
		    // dub: for the old-style speculation implementation, we 
		    // overload the packet priorities to prioritize non-
		    // speculative requests over speculative ones
		    if( _speculative == 1 )
		      _sw_allocator->AddRequest(expanded_input, expanded_output,
						vc, 0, 0);
		    else
		      _spec_sw_allocator->AddRequest(expanded_input, 
						     expanded_output, vc,
						     cur_vc->GetPriority( ), 
						     cur_vc->GetPriority( ));
		    vc_ready_spec++;
		  }
		}
		iset++;
	      }
	    }
	    break;
	  }
	}
	vc = ( vc + 1 ) % _vcs;
      }
    }
  }
  
  if(watched) {
    *gWatchOut << GetSimTime() << " | " << _sw_allocator->FullName() << " | ";
    _sw_allocator->PrintRequests( gWatchOut );
    if(_speculative >= 2) {
      *gWatchOut << GetSimTime() << " | " << _spec_sw_allocator->FullName() << " | ";
      _spec_sw_allocator->PrintRequests( gWatchOut );
    }
  }
  
  _sw_allocator->Allocate();
  if(_speculative >= 2)
    _spec_sw_allocator->Allocate();
  
  // Winning flits cross the switch

  _crossbar_pipe->WriteAll( 0 );

  //////////////////////////////
  // Switch Power Modelling
  //  - Record Total Cycles
  //
  switchMonitor.cycle() ;

  for ( int input = 0; input < _inputs; ++input ) {
    c = 0;
    
    int vc_grant_nonspec = 0;
    int vc_grant_spec = 0;
    
    for ( int s = 0; s < _input_speedup; ++s ) {

      bool use_spec_grant = false;
      
      expanded_input  = s*_inputs + input;

      if ( _switch_hold_in[expanded_input] != -1 ) {
	assert(_switch_hold_in[expanded_input] >= 0);
	expanded_output = _switch_hold_in[expanded_input];
	vc = _switch_hold_vc[expanded_input];
	assert(vc >= 0);
	cur_vc = _vc[input][vc];
	
	if ( cur_vc->Empty( ) ) { // Cancel held match if VC is empty
	  expanded_output = -1;
	}
      } else {
	expanded_output = _sw_allocator->OutputAssigned( expanded_input );
	if ( ( _speculative >= 2 ) && ( expanded_output < 0 ) ) {
	  expanded_output = _spec_sw_allocator->OutputAssigned(expanded_input);
	  if ( expanded_output >= 0 ) {
	    assert(_spec_sw_allocator->InputAssigned(expanded_output) >= 0);
	    assert(_spec_sw_allocator->ReadRequest(expanded_input, expanded_output) >= 0);
	    switch ( _filter_spec_grants ) {
	    case 0:
	      if ( any_nonspec_reqs )
		expanded_output = -1;
	      break;
	    case 1:
	      if ( any_nonspec_output_reqs[expanded_output] )
		expanded_output = -1;
	      break;
	    case 2:
	      if ( _sw_allocator->InputAssigned(expanded_output) >= 0 )
		expanded_output = -1;
	      break;
	    default:
	      assert(false);
	    }
	  }
	  use_spec_grant = (expanded_output >= 0);
	}
      }

      if ( expanded_output >= 0 ) {
	output = expanded_output % _outputs;

	if ( _switch_hold_in[expanded_input] == -1 ) {
	  if(use_spec_grant) {
	    assert(_spec_sw_allocator->OutputAssigned(expanded_input) >= 0);
	    assert(_spec_sw_allocator->InputAssigned(expanded_output) >= 0);
	    vc = _spec_sw_allocator->ReadRequest(expanded_input, 
						 expanded_output);
	  } else {
	    assert(_sw_allocator->OutputAssigned(expanded_input) >= 0);
	    assert(_sw_allocator->InputAssigned(expanded_output) >= 0);
	    vc = _sw_allocator->ReadRequest(expanded_input, expanded_output);
	  }
	  assert(vc >= 0);
	  cur_vc = _vc[input][vc];
	}

	// Detect speculative switch requests which succeeded when VC 
	// allocation failed and prevent the switch from forwarding;
	// also, in case the routing function can return multiple outputs, 
	// check to make sure VC allocation and speculative switch allocation 
	// pick the same output port.
	if ( ( ( cur_vc->GetState() == VC::vc_spec_grant ) ||
	       ( cur_vc->GetState() == VC::active ) ) &&
	     ( cur_vc->GetOutputPort() == output ) ) {
	  
	  if(use_spec_grant) {
	    vc_grant_spec++;
	  } else {
	    vc_grant_nonspec++;
	  }
	  
	  if ( _hold_switch_for_packet ) {
	    _switch_hold_in[expanded_input] = expanded_output;
	    _switch_hold_vc[expanded_input] = vc;
	    _switch_hold_out[expanded_output] = expanded_input;
	  }
	  
	  assert((cur_vc->GetState() == VC::vc_spec_grant) ||
		 (cur_vc->GetState() == VC::active));
	  assert(!cur_vc->Empty());
	  assert(cur_vc->GetOutputPort() == output);
	  
	  dest_vc = _next_vcs[output];
	  
	  if ( dest_vc->IsFullFor( cur_vc->GetOutputVC( ) ) )
	    continue ;
	  
	  // Forward flit to crossbar and send credit back
	  f = cur_vc->RemoveFlit( );
	  assert(f);
	  if(f->id==176) {
	    fp << GetSimTime() << " | " << FullName() << " | "
		       << "Output " << output
		       << " granted to VC " << vc << " at input " << input;
	    if(cur_vc->GetState() == VC::vc_spec_grant)
	      fp << " (spec";
	    else
	      fp << " (non-spec";
	    fp << ", exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ", flit: " << f->id << ")." << endl;
	  }
	  if(f->watch) {
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Output " << output
		       << " granted to VC " << vc << " at input " << input;
	    if(cur_vc->GetState() == VC::vc_spec_grant)
	      *gWatchOut << " (spec";
	    else
	      *gWatchOut << " (non-spec";
	    *gWatchOut << ", exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ", flit: " << f->id << ")." << endl;
	  }
	  
	  f->hops++;
	  //added by kvm
	  //increment number of flits in the present time interval
	this->Increment_present_flits(output);
	
	if(f->id==28760)
	{
	fp<<"curent router"<<this->GetID()<<endl;
	fp<<"NOF"<<this->GetNOF_port(output)<<endl;
	fp<<"out port"<<output<<endl;
	
	}
	//added by KVM
	//increment the number of flits passed through this particular router
	//this->IncrementNOF();
	
	//increment the number of flits passed through this router through a port
	this->IncrementNOF_port(output);
	
	
	
	
	if(f->id==28760)
	{
	fp<<"after incrementing"<<endl;
	fp<<"curent router"<<this->GetID()<<endl;
	fp<<"NOF"<<this->GetNOF_port(output)<<endl;
	fp<<"out port"<<output<<endl;
	}
	  
	  //
	  // Switch Power Modelling
	  //
	  switchMonitor.traversal( input, output, f) ;
	  bufferMonitor.read(input, f) ;
	  
	  if(f->id==176)
	    fp<< GetSimTime() << " | " << FullName() << " | "
		       << "Forwarding flit " << f->id << " through crossbar "
		       << "(exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ")." << endl;
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Forwarding flit " << f->id << " through crossbar "
		       << "(exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ")." << endl;
	  
	  if ( !c ) {
	    c = _NewCredit( _vcs );
	  }

	  assert(vc == f->vc);

	  c->vc[c->vc_cnt] = f->vc;
	  c->vc_cnt++;
	  c->dest_router = f->from_router;
	  f->vc = cur_vc->GetOutputVC( );
	  dest_vc->SendingFlit( f );
	  
	  _crossbar_pipe->Write( f, expanded_output ); // switch traversal takes place in this line
	  
	  if(f->tail) {
	    if(cur_vc->Empty()) {
	      cur_vc->SetState(VC::idle);
	    } else if(_routing_delay > 0) {
	      cur_vc->SetState(VC::routing);
	      _routing_vcs.push(input*_vcs+vc);
	    } else {
	      cur_vc->Route(_rf, this, cur_vc->FrontFlit(), input);
	      cur_vc->SetState(VC::vc_alloc);
	      _vcalloc_vcs.insert(input*_vcs+vc);
	    }
	    _switch_hold_in[expanded_input]   = -1;
	    _switch_hold_vc[expanded_input]   = -1;
	    _switch_hold_out[expanded_output] = -1;
	  } else {
	    // reset state timer for next flit
	    cur_vc->SetState(VC::active);
	  }
	  
	  _sw_rr_offset[expanded_input] = ( vc + 1 ) % _vcs;
	} else {
	  assert(cur_vc->GetState() == VC::vc_spec);
	  Flit * f = cur_vc->FrontFlit();
	  assert(f);
	  if(f->id==176)
	    fp << GetSimTime() << " | " << FullName() << " | "
		       << "Speculation failed at output " << output
		       << "(exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ", flit: " << f->id << ")." << endl;
	  if(f->watch)
	    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
		       << "Speculation failed at output " << output
		       << "(exp. input: " << expanded_input
		       << ", exp. output: " << expanded_output
		       << ", flit: " << f->id << ")." << endl;
	} 
      }
    }
    
    // Promote all other virtual channel grants marked as speculative to active.
    for ( int vc = 0 ; vc < _vcs ; vc++ ) {
      cur_vc = _vc[input][vc] ;
      if ( cur_vc->GetState() == VC::vc_spec_grant ) {
	cur_vc->SetState( VC::active ) ;	
      } 
    }
    
    _credit_pipe->Write( c, input ); // writing credits in backward direction
  }
}
