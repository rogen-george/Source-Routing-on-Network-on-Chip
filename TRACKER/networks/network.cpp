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

/*network.cpp
 *
 *This class is the basis of the entire network, it contains, all the routers
 *channels in the network, and is extended by all the network topologies
 *
 */

#include "booksim.hpp"
#include <assert.h>
#include "network.hpp"
#include <cmath>


// constructor and destructor....... of Network class
Network::Network( const Configuration &config, const string & name ) :  Module( 0, name )
{
	  _size     = -1; 
	  _sources  = -1; 
	  _dests    = -1;
	  _channels = -1;
}

Network::~Network( )
{
	  //cout<<"network destructor"<<endl;  // code for link utilization
	  //getchar();
	  int non_empty_channels=0, zerolink_channels=0,sum_NOF=0,each_NOF;
	  float avg_NOF=0.0,sum_std_dev_NOF=0.0,ff=0.0,std_dev=0.0;
	  cout<<"channel utilization"<<endl;
	  cout<<"**********************"<<endl;
	  for(int r=0;r<_size;++r)
	  {
	  	//cout<<"R-"<<r;
	  	for(int c=0;c< 4;++c)
	  	{
	  		each_NOF=_routers[r]->GetNOF_port(c);
	  		if(each_NOF!=0)
	  			non_empty_channels++;
	  		//cout<<" "<<_routers[r]->GetNOF_port(c);
	  		sum_NOF+=each_NOF;
	  	}
	  	//cout<<endl;
	  }
	  cout<<"num of non-empty channels- "<<non_empty_channels<<endl;
          cout<<"Totalflits- "<<sum_NOF<<endl;			
	  zerolink_channels=64-non_empty_channels;  // constant is total nodes (For 4x4 mesh 16x 4= "64")
	  avg_NOF=(float)sum_NOF/non_empty_channels;  // number of links
          cout<<"Avg-flits- "<<avg_NOF<<endl;
	  for(int r=0;r<_size;++r)
	  {
	  	for(int c=0;c< 4;++c)
	  	{
	  		each_NOF=_routers[r]->GetNOF_port(c);
	  		 sum_std_dev_NOF+=abs(each_NOF-avg_NOF);
	  		//sum_std_dev_NOF+=(each_NOF-avg_NOF)*(each_NOF-avg_NOF);
	  	}
	  }
	 
	  sum_std_dev_NOF=sum_std_dev_NOF-(zerolink_channels*avg_NOF);
          cout<<"total flit deviation- "<<sum_std_dev_NOF<<endl;
	  //std_dev=sqrt(sum_std_dev_NOF/non_empty_channels);
	  std_dev=sum_std_dev_NOF/(non_empty_channels);
	  cout<<"avg flit deviation- "<<std_dev<<endl;
		//**********************************
	  	//cout<<"LUF>SD= "<<(int)std_dev<<endl;
		//**********************************
	  ff=avg_NOF/std_dev;
		//**********************************
	  	cout<<"LUF= "<<ff<<endl;
		//**********************************
	  for ( int r = 0; r < _size; ++r )
	  {
	    	   
	    	   if ( _routers[r] ) delete _routers[r];
	  }
	  for ( int s = 0; s < _sources; ++s )
	  {
		    
		    if ( _inject[s] ) delete _inject[s];
		    if ( _inject_cred[s] ) delete _inject_cred[s];
	  }
	  for ( int d = 0; d < _dests; ++d )
	  {
		    
		    if ( _eject[d] ) delete _eject[d];
		    if ( _eject_cred[d] ) delete _eject_cred[d];
	  }
	  for ( int c = 0; c < _channels; ++c )
	  {
		    
		    if ( _chan[c] ) delete _chan[c];
		    if ( _chan_cred[c] ) delete _chan_cred[c];
	  }
	  
	
}


//==================================================================================


void Network::_Alloc( )
{
	//cout<<"alloc"<<endl;
	// allocating flit channel and credit channel for each source destination and channel
	assert( ( _size != -1 ) && ( _sources != -1 ) &&  ( _dests != -1 ) &&  ( _channels != -1 ) ); // abort if result!=0
	_routers.resize(_size);
	gNodes = _sources;
	  /*booksim used arrays of flits as the channels which makes have capacity of 
	   *one. To simulate channel latency, flitchannel class has been added
	   *which are fifos with depth = channel latency and each cycle the channel
	   *shifts by one
	   *credit channels are the necessary counter part
	   */
	_inject.resize(_sources);
	_inject_cred.resize(_sources);
	for ( int s = 0; s < _sources; ++s )  // do for each source node
	{
		_inject[s] = new FlitChannel;
		_inject_cred[s] = new CreditChannel;
	}
	_eject.resize(_dests);
	_eject_cred.resize(_dests);
	for ( int d = 0; d < _dests; ++d ) // do for each destination
	{
		_eject[d] = new FlitChannel;
		_eject_cred[d] = new CreditChannel;
	}
	_chan.resize(_channels);
	_chan_cred.resize(_channels);
	for ( int c = 0; c < _channels; ++c ) // do for each channel
	{
		_chan[c] = new FlitChannel;
	    	_chan_cred[c] = new CreditChannel;
	}

	_chan_use.resize(_channels, 0);
	_chan_use_cycles = 0;
	//cout<<"end-alloc"<<endl;

} // end _Alloc
//---------------------------------------------------------------------------------
int Network::NumSources( ) const
{
	return _sources;
}

int Network::NumDests( ) const
{
	return _dests;
}

void Network::ReadInputs( )
{
	 for ( int r = 0; r < _size; ++r )
	 {
	    	_routers[r]->ReadInputs( );
	 }
}

void Network::InternalStep( )
{
	 for ( int r = 0; r < _size; ++r )
	 {
	    	_routers[r]->InternalStep( );
	 }
}
void Network::update_wnof_values()
{
	//for wnof
	for ( int r = 0; r < _size; ++r )
	{
		_routers[r]->update_wnof_values();
	}
	
}
void Network::update_nop_values()
{
	//for nop
	for ( int r = 0; r < _size; ++r )
	{
		_routers[r]->update_nop_values();
	}
	
}

void Network::Clear_has_input()	//fluidity
{
	for ( int r = 0; r < _size; ++r )
		{
			_routers[r]->Clear_has_input();
		}
}

void Network::Update_cum_time_out()
{
	for ( int r = 0; r < _size; ++r )
	{
		_routers[r]->Update_cum_time_out();
	}
	
}


void Network::WriteOutputs( )
{
	 for ( int r = 0; r < _size; ++r )
	 {
	    	_routers[r]->WriteOutputs( );
	 }

  	for ( int c = 0; c < _channels; ++c )
	{
    		if ( _chan[c]->InUse() )
		 {
      			_chan_use[c]++;
    		 }
  	}
  	_chan_use_cycles++;
  	
  	for( int r=0; r< _size; r++)	//fluidity
  	{
  		_routers[r]->Update_fluidity();
  	}
  		
}
void Network::WriteFlit( Flit *f, int source )
{
  assert( ( source >= 0 ) && ( source < _sources ) );
  _inject[source]->Send(f);
}

Flit *Network::ReadFlit( int dest )
{
  assert( ( dest >= 0 ) && ( dest < _dests ) );
  return _eject[dest]->Receive();
}

/* new functions added for NOC
 */
Flit* Network::PeekFlit( int dest ) 
{
	  assert( ( dest >= 0 ) && ( dest < _dests ) );
	  return _eject[dest]->Peek( );
}

void Network::WriteCredit( Credit *c, int dest )
{
	  assert( ( dest >= 0 ) && ( dest < _dests ) );
	  _eject_cred[dest]->Send(c);
}

Credit *Network::ReadCredit( int source )
{
	  assert( ( source >= 0 ) && ( source < _sources ) );
	  //Receive() returns the next element in the queue of injected credits at source 
	  return _inject_cred[source]->Receive();  //Receive() is in channel.hpp
}

/* new functions added for NOC
 */
Credit *Network::PeekCredit( int source ) 
{
	  assert( ( source >= 0 ) && ( source < _sources ) );
	  return _inject_cred[source]->Peek( );
}

void Network::InsertRandomFaults( const Configuration &config )
{
  	Error( "InsertRandomFaults not implemented for this topology!" );
}

void Network::OutChannelFault( int r, int c, bool fault )
{
	  assert( ( r >= 0 ) && ( r < _size ) );
	  _routers[r]->OutChannelFault( c, fault );
}

double Network::Capacity( ) const
{
  	return 1.0;
}

/* this function can be heavily modified to display any information
 * neceesary of the network, by default, call display on each router
 * and display the channel utilization rate
 */
void Network::Display( ) const
{
	  for ( int r = 0; r < _size; ++r )
	  {
	    	_routers[r]->Display( );
	  }
	  double average = 0;
	  for ( int c = 0; c < _channels; ++c )
	 {
	    	cout << "channel " << c << " used " << 100.0 * (double)_chan_use[c] / (double)_chan_use_cycles 
		 << "% of the time" << endl;
	   	average += 100.0 * (double)_chan_use[c] / (double)_chan_use_cycles ;
	  }
	  average = average/_channels;
	  cout<<"Average channel: "<<average<<endl;
}
