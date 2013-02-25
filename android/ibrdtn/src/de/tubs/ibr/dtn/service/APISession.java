/*
 * APISession.java
 * 
 * Copyright (C) 2011 IBR, TU Braunschweig
 *
 * Written-by: Johannes Morgenroth <morgenroth@ibr.cs.tu-bs.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
package de.tubs.ibr.dtn.service;

import ibrdtn.api.APIConnection;
import ibrdtn.api.ExtendedClient;
import ibrdtn.api.ExtendedClient.APIException;
import ibrdtn.api.sab.DefaultSABHandler;
import ibrdtn.api.sab.SABException;

import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;

import de.tubs.ibr.dtn.api.Block;
import de.tubs.ibr.dtn.api.Bundle;
import de.tubs.ibr.dtn.api.BundleID;
import de.tubs.ibr.dtn.api.TransferMode;
import de.tubs.ibr.dtn.api.DTNSessionCallback;
import de.tubs.ibr.dtn.api.GroupEndpoint;
import de.tubs.ibr.dtn.api.Registration;
import de.tubs.ibr.dtn.api.SessionDestroyedException;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;

public class APISession {
	
	private final String TAG = "APISession";

    // client connection which is connected to the local daemon
//    private ExtendedClient client = null;
    
    private ClientSession session = null;
    
    // callback to the service-client
    private Object _callback_mutex = new Object();
    
    // mutex for operation on the API
    private Object _api_mutex = new Object();
    
    private DTNSessionCallback _callback_real = null;
    private DTNSessionCallback _callback_wrapper = new DTNSessionCallback() {

		public IBinder asBinder() {
			return null;
		}

		public void startBundle(Bundle bundle) throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return;
				try {
					_callback_real.startBundle(bundle);
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [startBundle]", e);
				}
			}
		}

		public void endBundle() throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return;
				try {
					_callback_real.endBundle();
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [endBundle]", e);
				}
			}
		}

		public TransferMode startBlock(Block block) throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return TransferMode.NULL;
				try {
					return _callback_real.startBlock(block);
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [startBlock]", e);
					return TransferMode.NULL;
				}
			}
		}

		public void endBlock() throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return;
				try {
					_callback_real.endBlock();
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [endBlock]", e);
				}
			}
		}

		public ParcelFileDescriptor fd() throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return null;
				try {
					return _callback_real.fd();
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [fd]", e);
				}
				
				return null;
			}
		}

		public void progress(long current, long length) throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return;
				try {
					_callback_real.progress(current, length);
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [progress]", e);
				}
			}
		}

		public void payload(byte[] data) throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return;
				try {
					_callback_real.payload(data);
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [payload]", e);
				}
			}
		}

		public void characters(String data) throws RemoteException {
			synchronized(_callback_mutex) {
				if (_callback_real == null) return;
				try {
					_callback_real.characters(data);
				} catch (Exception e) {
					// remove the callback on error
					_callback_real = null;
					Log.e(TAG, "Error on callback [characters]", e);
				}
			}
		}
    };
    
    private enum SessionState {
    	UNINITIALIZED,
    	CONNECTED,
    	CONNECTING,
    	DISCONNECTING,
    	DISCONNECTED,
    	FAILED
    };
    
    private Object _state_mutex = new Object();
    private SessionState _state = SessionState.UNINITIALIZED;
    
	private void setState(SessionState s) {
		synchronized(_state_mutex) {
			_state = s;
			_state_mutex.notifyAll();
			Log.d(TAG, "State set to " + s.toString());
		}
	}
    
    public APISession(ClientSession session)
    {
    	this.session = session;
    }
    
    private void raise_callback_failure(Exception e)
    {
    	Log.e(TAG, "Callback failure", e);
    }
    
//    private void raise_connection_failure(Exception e)
//    {
//    	Log.d(TAG, "connection exception raised: " + e.getMessage());
//    	
//    	if (client != null)
//    	{
//    		if (!client.isConnected())
//    		{
//    			Log.d(TAG, "client is no longer connected");
//    			
//    	    	// destroy connection
//    	    	disconnect();
//    		}
//    		else
//    		{
//        		try {
//    				// check api connection
//    				this.client.noop();
//    				return;
//    			} catch (APIException ex) {
//        	    	// destroy connection
//        	    	disconnect();
//    			}
//    		}    		
//    	}
//    	
////    	session.invoke_reconnect();
//    }
    
    public void connect()
    {
//    	synchronized(_state_mutex) {
//    		if ((_state == SessionState.DISCONNECTED) || (_state == SessionState.FAILED)) throw new SessionDestroyedException();
//    		if (_state != SessionState.UNINITIALIZED)
//    		{
//    			Log.e(TAG, "APISession in invalid state (" + _state.toString() + ") for connect()");
//    			return;
//    		}
//    		setState(SessionState.CONNECTING);
//    	}
//
//    	synchronized(_api_mutex) {
//	    	// create a new extended client (connection to the daemon)
//			this.client = new ExtendedClient();
//			this.client.setConnection( conn );
//			this.client.setHandler(sabhandler);
//			this.client.setDebug( Log.isLoggable(TAG, Log.DEBUG) );
//			
//			try {
//				this.client.open();
				setState(SessionState.CONNECTED);
//			} catch (IOException e) {
//				setState(SessionState.FAILED);
//				raise_connection_failure(e);
//			}
//    	}
    }
    
    public void disconnect()
    {
//    	synchronized(_state_mutex) {
//    		try {
//	    		while (_state == SessionState.CONNECTING)
//	    		{
//	    			_state_mutex.wait();
//	    		}
//    		} catch (InterruptedException e) {
//    			setState(SessionState.FAILED);
//    			return;
//    		}
//    		
//			// just return if the connection is not open
//			if (_state != SessionState.CONNECTED) return;
//			setState(SessionState.DISCONNECTING);
//    	}
//    	
//		try {
//			this.client.close();
//		} catch (IOException e) { }
		
		setState(SessionState.DISCONNECTED);
    }
    
    public void register(Registration reg) throws APIException
    {
//		try {
			synchronized(_api_mutex) {
				// set endpoint id
//				client.setEndpoint(reg.getEndpoint());
				NativeDaemonWrapper.setEndpoint(reg.getEndpoint());
			}
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "endpoint registered: " + reg.getEndpoint());
//		} catch (APIException e) {
//			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "endpoint registration failed: " + reg.getEndpoint());
//			raise_connection_failure(e);
//			throw e;
//		}
		
		for (GroupEndpoint group : reg.getGroups())
		{
			ibrdtn.api.object.GroupEndpoint eid = new ibrdtn.api.object.GroupEndpoint( group.toString() );
			
			// register to presence messages
//			try {
				synchronized(_api_mutex) {
//					client.addRegistration(eid);
					NativeDaemonWrapper.addRegistration(eid.toString());
				}
				if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "registration added: " + eid);
//			} catch (APIException e) {
//				if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "registration add failed: " + eid);
//				raise_connection_failure(e);
//				throw e;
//			}
		}
    }
    
    public Boolean isConnected()
    {
    	return (_state == SessionState.CONNECTED);
    }
    
	/**
	 * Query for a specific bundle.
	 * @param id
	 * @throws SessionDestroyedException
	 */
    public Boolean query(DTNSessionCallback cb, BundleID id) throws SessionDestroyedException
	{
		if (!isConnected()) throw new SessionDestroyedException("not connected.");
		ibrdtn.api.object.BundleID api_id = new ibrdtn.api.object.BundleID();
		id.setSource(id.getSource());
		
		ibrdtn.api.Timestamp ts = new ibrdtn.api.Timestamp( id.getTimestamp() );
		api_id.setTimestamp(ts.getValue());
		
		id.setSequencenumber(id.getSequencenumber());
		
//		try {
			synchronized(_api_mutex) {
				// load the next bundle
				if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "load bundle: " + id.toString());
//				client.loadBundle(api_id);
				NativeDaemonWrapper.loadBundle(api_id.toString());
			
				// set callback and mode
		    	synchronized(_callback_mutex) {
		    		this._callback_real = cb;
		    	}
			
		    	if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "get bundle: " + id.toString());
//		    	client.getBundle();
		    	NativeDaemonWrapper.getBundle();
			}
			
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "load successful");
			return true;
//		} catch (APIException e) {
//			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "load failed");
//			raise_connection_failure(e);
//			return false;
//		}
	}
    
	/**
	 * Query for the next bundle in the queue.
	 * @throws SessionDestroyedException
	 */
    public Boolean query(DTNSessionCallback cb) throws SessionDestroyedException
	{
		if (!isConnected()) throw new SessionDestroyedException("not connected.");
		
//		try {
			synchronized(_api_mutex) {
				// load the next bundle
				if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "load and get bundle");

				// set callback and mode
		    	synchronized(_callback_mutex) {
		    		this._callback_real = cb;
		    	}
	    	
		    	// execute query
//				this.client.loadAndGetBundle();
				NativeDaemonWrapper.loadAndGetBundle();
			}
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "load successful");
			return true;
//		} catch (APIException e) {
//			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "load failed");
//			raise_connection_failure(e);
//			return false;
//		}
	}
	
	/**
	 * Mark a bundle as delivered.
	 * @param id
	 * @throws SessionDestroyedException
	 */
    public void setDelivered(BundleID id) throws SessionDestroyedException
	{
		if (!isConnected()) throw new SessionDestroyedException("not connected.");
		
//		try {
			ibrdtn.api.object.BundleID api_id = new ibrdtn.api.object.BundleID();
			api_id.setSource(id.getSource());
			
			ibrdtn.api.Timestamp ts = new ibrdtn.api.Timestamp( id.getTimestamp() );
			api_id.setTimestamp(ts.getValue());
			
			api_id.setSequencenumber(id.getSequencenumber());
			
			synchronized(_api_mutex) {
				// check for bundle to ack
				if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "ack bundle: " + id.toString());

//				client.markDelivered(api_id);
				NativeDaemonWrapper.markDelivered(api_id.toString());
			}
//		} catch (APIException e) {
//			raise_connection_failure(e);
//		};
	}
	
    public Boolean send(de.tubs.ibr.dtn.api.EID destination, int lifetime, byte[] data) throws SessionDestroyedException
	{
		if (!isConnected()) throw new SessionDestroyedException("not connected.");
		
//		try {
			ibrdtn.api.object.EID api_destination = null;
			
			if (destination instanceof de.tubs.ibr.dtn.api.SingletonEndpoint)
			{
				api_destination = new ibrdtn.api.object.SingletonEndpoint( destination.toString() );
			}
			else
			{
				api_destination = new ibrdtn.api.object.GroupEndpoint( destination.toString() );
			}
			
			synchronized(_api_mutex) {
				// send the message to the daemon
//				this.client.send(api_destination, lifetime, data);
				NativeDaemonWrapper.send(api_destination, lifetime, data);
			}
			
			// debug
			Log.i(TAG, "Message sent: " + data);
//		} catch (APIException e) {
//			Log.e(TAG, "could not send the message: ", e);
//			raise_connection_failure(e);
//			return false;
//		} catch (Exception e) {
//			Log.e(TAG, "could not send the message: ", e);
//			raise_connection_failure(e);
//			return false;
//		}
		
		return true;
	}
	
    public Boolean send(de.tubs.ibr.dtn.api.EID destination, int lifetime, ParcelFileDescriptor fd, long length) throws SessionDestroyedException
	{
		if (!isConnected()) throw new SessionDestroyedException("not connected.");
		
		ibrdtn.api.object.EID api_destination = null;
		
		if (destination instanceof de.tubs.ibr.dtn.api.SingletonEndpoint)
		{
			api_destination = new ibrdtn.api.object.SingletonEndpoint( destination.toString() );
		}
		else
		{
			api_destination = new ibrdtn.api.object.GroupEndpoint( destination.toString() );
		}
		
		if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "Received file descriptor as bundle payload.");
		
		FileInputStream stream = new FileInputStream(fd.getFileDescriptor());
		try {
			synchronized(_api_mutex) {
//				this.client.send(api_destination, lifetime, stream, length);
				NativeDaemonWrapper.send(api_destination, lifetime, stream, length);
			}
			return true;
//		} catch (APIException e) {
//			Log.e(TAG, "Sending file descriptor content failed: ", e);
//			raise_connection_failure(e);
//			return false;
		} finally {
			try {
				stream.close();
				fd.close();
			} catch (IOException e) { }
		}
	}
	
	private DefaultSABHandler sabhandler = new DefaultSABHandler() {

		Bundle current_bundle = null;
		Block current_block = null;
		TransferMode current_callback = null;
		ParcelFileDescriptor fd = null;
		ByteArrayOutputStream stream = null;
		CountingOutputStream counter = null;
		BufferedWriter output = null;
		
		Long progress_last = 0L;
		
		// 0 = initial
		// 1 = receiving
		// 2 = done
		int progress_state = 0;
		
		private void updateProgress()
		{
			if (current_block == null) return;
			if (current_block.length == null) return;
			if (current_block.type != 1) return;
			
			switch (progress_state)
			{
			// initial state
			case 0:
				// new block, announce zero
				try {
					_callback_wrapper.progress(0, current_block.length);
				} catch (RemoteException e1) {
					raise_callback_failure(e1);
				}
				
				progress_state = 1;
				progress_last = 0L;
				break;
				
			case 1:
				if (counter != null)
				{
					long newcount = counter.getCount();
					if (newcount != progress_last)
					{
						// only announce if the 5% has changed
						if ( (current_block.length / 20) <= (newcount - progress_last) )
						{
							try {
								_callback_wrapper.progress(newcount, current_block.length);
							} catch (RemoteException e1) {
								raise_callback_failure(e1);
							}

							progress_last = newcount;
						}
					}
				}
				break;
				
			case 2:
				try {
					_callback_wrapper.progress(current_block.length, current_block.length);
				} catch (RemoteException e1) {
					raise_callback_failure(e1);
				}
				progress_state = 0;
				break;
			}
		}

		@Override
		public void startBundle() {
			current_bundle = new Bundle();
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "startBundle");
		}

		@Override
		public void endBundle() {
			try {
				_callback_wrapper.endBundle();
			} catch (RemoteException e1) {
				raise_callback_failure(e1);
			}
			
			current_bundle = null;
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "endBundle");
		}

		@Override
		public void startBlock(Integer type) {
			try {
				_callback_wrapper.startBundle(current_bundle);
			} catch (RemoteException e1) {
				raise_callback_failure(e1);
			}

			current_block = new Block();
			current_block.type = type;
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "startBlock: " + String.valueOf(type));
			
			// set callback to null. This results in a query for the right
			// callback strategy at the beginning of the payload.
			current_callback = null;
		}

		private void initializeBlock() {
			// announce this block and determine the payload transfer method
			try {
				current_callback = _callback_wrapper.startBlock(current_block);
			} catch (RemoteException e1) {
				raise_callback_failure(e1);
			}

			switch (current_callback) {
			default:
				break;

			case FILEDESCRIPTOR:
				// request file descriptor if requested by client
				try {
					fd = _callback_wrapper.fd();
				} catch (RemoteException e1) {
					raise_callback_failure(e1);
				}
				
				if (fd != null)
				{
					if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "FD received");
					
					counter = new CountingOutputStream( new FileOutputStream(fd.getFileDescriptor()) );
					output = new BufferedWriter( 
								new OutputStreamWriter(
									new de.tubs.ibr.dtn.util.Base64.OutputStream( 
										counter,
										de.tubs.ibr.dtn.util.Base64.DECODE
										)
									)
								);
				}
				break;

			case SIMPLE:
				// in simple mode, we just create a bytearray stream
				stream = new ByteArrayOutputStream();
				counter = new CountingOutputStream( stream );
				output = new BufferedWriter( 
						new OutputStreamWriter(
							new de.tubs.ibr.dtn.util.Base64.OutputStream( 
								counter,
								de.tubs.ibr.dtn.util.Base64.DECODE
								)
							)
						);
				break;
			}
		}

		@Override
		public void endBlock() {
			// close output stream of the last block
			if (output != null)
			{
				try {
					output.flush();
					output.close();
				} catch (IOException e) { };
				
				output = null;
				counter = null;
			}
			
			// close filedescriptor if there one
			if (fd != null)
			{
				try {
					fd.close();
				} catch (IOException e) { };
				
				fd = null;
			}
			else if (stream != null)
			{
				try {
					stream.close();
				} catch (IOException e) { };
				
				try {
					_callback_wrapper.payload(stream.toByteArray());
				} catch (RemoteException e1) {
					raise_callback_failure(e1);
				}
				
				stream = null;
			}
			
			try {
				_callback_wrapper.endBlock();
			} catch (RemoteException e1) {
				raise_callback_failure(e1);
			}
			
			// update progress stats
			progress_state = 2;
			updateProgress();
			
			current_block = null;
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "endBlock");
		}

		@Override
		public void attribute(String keyword, String value) {
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "attribute " + keyword + " = " + value);
			
			if (current_bundle != null)
			{
				if (current_block == null)
				{
					if (keyword.equalsIgnoreCase("source")) {
						current_bundle.source = value;
					}
					else if (keyword.equalsIgnoreCase("destination")) {
						current_bundle.destination = value;
					}
					else if (keyword.equalsIgnoreCase("timestamp")) {
						ibrdtn.api.Timestamp ts = new ibrdtn.api.Timestamp( Long.parseLong(value) );
						current_bundle.timestamp = ts.getDate();
					}
					else if (keyword.equalsIgnoreCase("sequencenumber")) {
						current_bundle.sequencenumber = Long.parseLong(value);
					}
				}
				else
				{
					if (keyword.equalsIgnoreCase("length")) {
						current_block.length = Long.parseLong(value);
					}
				}
			}
		}

		@Override
		public void characters(String data) throws SABException {
			if (Log.isLoggable(TAG, Log.DEBUG)) Log.d(TAG, "characters: " + data);
			
			// if the current callback strategy is set to null
			// then announce the block via the startBlock() callback and
			// ask for the right callback strategy.
			if (current_callback == null) initializeBlock();

			switch (current_callback)
			{
			case PASSTHROUGH:
				try {
					_callback_wrapper.characters(data);
				} catch (RemoteException e1) {
					raise_callback_failure(e1);
				}
				break;

			default:
				if (output != null)
				{
					try {
						output.append(data);
					} catch (IOException e) {
						if (Log.isLoggable(TAG, Log.DEBUG)) Log.e(TAG, "Can not write data to output stream.", e);
					}
				}
				break;
			}
			
			// update progress stats
			updateProgress();
		}

		@Override
		public void notify(Integer type, String data) {
			Log.i(TAG, "new notification " + String.valueOf(type) + ": " + data);
			if (type == 602)
			{
				session.invoke_receive_intent(new BundleID());
			}
		}
	};
}
