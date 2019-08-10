/*
 * DTNClient.java
 * 
 * Copyright (C) 2011 IBR, TU Braunschweig
 *
 * Written-by: Johannes Morgenroth <morgenroth@ibr.cs.tu-bs.de>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package de.tubs.ibr.dtn.api;

import java.util.List;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.Parcelable;
import android.os.RemoteException;
import android.preference.PreferenceManager;
import android.util.Log;
import de.tubs.ibr.dtn.DTNService;
import de.tubs.ibr.dtn.Services;

public final class DTNClient {
	
	private static final String TAG = "DTNClient";
	
	private static final String PREF_SESSION_KEY = "dtn_session_key";

	// DTN service provided by IBR-DTN
	private DTNService mService = null;
	
	private Context mContext = null;
	
	private Registration mRegistration = null;
	
	private SessionConnection mSessionHandler = null;
	
	// marker to know if the client was initialized
	private Boolean mInitialized = false;
	
	// marker to know if the service is connected
	private Boolean mConnected = false;
	
	public DTNClient(SessionConnection handler) {
		mSessionHandler = handler;
	}
	
	/**
	 * May return null if the service is not connected
	 * @return
	 */
	public String getEndpoint() {
		if (mConnected) {
			try {
				return mService.getEndpoint();
			} catch (RemoteException e) {
				return null;
			}
		} else {
			return null;
		}
	}
	
	public synchronized PendingIntent getSelectNeighborIntent() {
		if (!mConnected) return null;
		
		try {
			android.os.Bundle b = mService.getSelectNeighborIntent();
			return b.getParcelable(de.tubs.ibr.dtn.Intent.EXTRA_PENDING_INTENT);
		} catch (RemoteException e) {
		}
		
		return null;
	}
	
	public synchronized List<Node> getNeighbors() {
		if (!mConnected) return null;
		try {
			return mService.getNeighbors();
		} catch (RemoteException e) {
			return null;
		}
	}
	
	private ServiceConnection mConnection = new ServiceConnection() {
		public void onServiceConnected(ComponentName name, IBinder service) {
			if (service == null) {
				// handle error (wrong API version)
				terminate();
			} else {
				mService = DTNService.Stub.asInterface(service);
				initializeSession(mRegistration);
			}
		}

		public void onServiceDisconnected(ComponentName name) {
			if (mConnected) {
				// set state to disconnected
				mConnected = false;
				
				// announce disconnected state
				mSessionHandler.onSessionDisconnected();
			}
			
			// clear local variables
			mService = null;
		}
	};
	
	private BroadcastReceiver mStateReceiver = new BroadcastReceiver() {
		@Override
		public void onReceive(Context context, Intent intent) {
			if (intent.getAction().equals(de.tubs.ibr.dtn.Intent.REGISTRATION))
			{
				Log.i(TAG, "registration successful");
				
				// store session key
				String session_key = intent.getStringExtra("key");
				
				SharedPreferences pm = PreferenceManager.getDefaultSharedPreferences(mContext);
				Editor edit = pm.edit();
				
				edit.putString(PREF_SESSION_KEY, session_key);
				
				// store new hash code
				edit.putInt("registration_hash", mRegistration.hashCode());
				
				edit.commit();
				
				// initialize the session
				initializeSession(mRegistration);
			}
		}
	};
	
	private void initializeSession(Registration reg) {
		Session s = null;
		
		try {
			// do not initialize if the service is not connected
			if (mService == null) return;
			
			s = new Session(mContext, mService);
			
			s.initialize(reg);
		} catch (Exception e) {
			try {
				Intent registrationIntent = Services.SERVICE_APPLICATION.getIntent(mContext, de.tubs.ibr.dtn.Intent.REGISTER);
				registrationIntent.putExtra("app", PendingIntent.getBroadcast(mContext, 0, new Intent(), 0)); // boilerplate
				registrationIntent.putExtra("registration", (Parcelable)reg);
				mContext.startService(registrationIntent);
				return;
			} catch (ServiceNotAvailableException err) {
				Log.e(TAG, "Service not available!", err);
			}
		}
		
		// set state to connected
		mConnected = true;
		
		// announce connected state
		mSessionHandler.onSessionConnected(s);
	}
	
	public static class Session
	{
		private Context mContext = null;
		private DTNService mService = null;
		private DTNSession mSession = null;
		
		// data handler which processes incoming bundles
		private DataHandler mHandler = null;
		
		public synchronized void setDataHandler(DataHandler handler) {
			mHandler = handler;
		}

		private de.tubs.ibr.dtn.api.DTNSessionCallback mCallback = new de.tubs.ibr.dtn.api.DTNSessionCallback.Stub() {
			public void startBundle(Bundle bundle) throws RemoteException {
				if (mHandler == null) return;
				
				try {
					mHandler.startBundle(bundle);
				} catch (Exception e) {
					Log.e(TAG, "Exception thrown in API callback!", e);
				}
			}

			public void endBundle() throws RemoteException {
				if (mHandler == null) return;
				try {
					mHandler.endBundle();
				} catch (Exception e) {
					Log.e(TAG, "Exception thrown in API callback!", e);
				}
			}

			public TransferMode startBlock(Block block) throws RemoteException {
				if (mHandler == null) return TransferMode.NULL;
				try {
					return mHandler.startBlock(block);
				} catch (Exception e) {
					Log.e(TAG, "Exception thrown in API callback!", e);
				}
				return TransferMode.NULL;
			}

			public void endBlock() throws RemoteException {
				if (mHandler == null) return;
				try {
					mHandler.endBlock();
				} catch (Exception e) {
					Log.e(TAG, "Exception thrown in API callback!", e);
				}
			}

			public ParcelFileDescriptor fd() throws RemoteException {
				if (mHandler == null) return null;
				try {
					return mHandler.fd();
				} catch (Exception e) {
					Log.e(TAG, "Exception thrown in API callback!", e);
				}
				return null;
			}

			public void progress(long current, long length) throws RemoteException {
				if (mHandler == null) return;
				try {
					mHandler.progress(current, length);
				} catch (Exception e) {
					Log.e(TAG, "Exception thrown in API callback!", e);
				}
			}

			public void payload(byte[] data) throws RemoteException {
				if (mHandler == null) return;
				try {
					mHandler.payload(data);
				} catch (Exception e) {
					Log.e(TAG, "Exception thrown in API callback!", e);
				}
			}
		};
		
		public Session(Context context, DTNService service) {
			mContext = context;
			mService = service;
		}
		
		public void initialize(Registration reg) throws RemoteException, SessionDestroyedException {
			SharedPreferences pm = PreferenceManager.getDefaultSharedPreferences(mContext);
			if (!pm.contains(PREF_SESSION_KEY)) {
				Log.i(TAG, "No session key available, need to register!");
				throw new SessionDestroyedException();
			}
			
			// check if registration has been changed since the last start
			if (reg != null) {
				if (pm.getInt("registration_hash", 0) != reg.hashCode()) {
					// update registration
					Log.i(TAG, "Registration has been changed, need to register!");
					throw new SessionDestroyedException();
				}
			}
			
			// try to resume the previous session
			mSession = mService.getSession(pm.getString(PREF_SESSION_KEY, ""));
			
			if (mSession == null)
			{
				Log.i(TAG, "Session not available, need to register!");
				throw new SessionDestroyedException();
			}
			
			Log.i(TAG, "session initialized");
		}
		
		public void destroy() {
			if (mSession == null) return;

			// send intent to destroy the session in the daemon
			try {
				Intent registrationIntent = Services.SERVICE_APPLICATION.getIntent(mContext, de.tubs.ibr.dtn.Intent.UNREGISTER);
				registrationIntent.putExtra("app", PendingIntent.getBroadcast(mContext, 0, new Intent(), 0)); // boilerplate
				mContext.startService(registrationIntent);
			} catch (ServiceNotAvailableException err) {
				Log.e(TAG, "Service not available!", err);
			}
		}
		
		/**
		 * Send a string as a bundle to the given destination.
		 */
		public BundleID send(Bundle bundle, byte[] data) throws SessionDestroyedException {
			if (mSession == null) throw new SessionDestroyedException("session is null");
			
			try {
				// send the message to the daemon
				return mSession.sendByteArray(null, bundle, data);
			} catch (RemoteException e) {
				 throw new SessionDestroyedException("send failed");
			}
		}
		
		/**
		 * Send a string as a bundle to the given destination.
		 */
		public BundleID send(EID destination, long lifetime, byte[] data) throws SessionDestroyedException {
			Bundle bundle = new Bundle();
			bundle.setDestination(destination);
			bundle.setLifetime(lifetime);
			
			return send(bundle, data);
		}
		
		/**
		 * Send the content of a file descriptor as bundle
		 */
		public BundleID send(Bundle bundle, ParcelFileDescriptor fd) throws SessionDestroyedException {
			if (mSession == null) throw new SessionDestroyedException("session is null");
			
			try {
				// send the message to the daemon
				return mSession.sendFileDescriptor(null, bundle, fd);
			} catch (RemoteException e) {
				return null;
			}
		}
		
		/**
		 * Send the content of a file descriptor as bundle
		 */
		public BundleID send(EID destination, long lifetime, ParcelFileDescriptor fd) throws SessionDestroyedException {
			Bundle bundle = new Bundle();
			bundle.setDestination(destination);
			bundle.setLifetime(lifetime);
			
			return send(bundle, fd);
		}
		
		public Boolean queryNext() throws SessionDestroyedException {
			if (mSession == null) throw new SessionDestroyedException("session is null");
			
			try {
				return mSession.queryNext(mCallback);
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
		
		public Boolean query(BundleID id) throws SessionDestroyedException {
			if (mSession == null) throw new SessionDestroyedException("session is null");
			
			try {
				return mSession.query(mCallback, id);
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
		
		public Boolean queryInfoNext() throws SessionDestroyedException {
			if (mSession == null) throw new SessionDestroyedException("session is null");
			
			try {
				return mSession.queryInfoNext(mCallback);
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
		
		public Boolean queryInfo(BundleID id) throws SessionDestroyedException {
			if (mSession == null) throw new SessionDestroyedException("session is null");
			
			try {
				return mSession.queryInfo(mCallback, id);
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
		
		public void delivered(BundleID id) throws SessionDestroyedException {
			if (mSession == null) throw new SessionDestroyedException("session is null");
			
			try {
				if (!mSession.delivered(id))
					throw new SessionDestroyedException("remote session error");
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
		
		public void join(GroupEndpoint group) throws SessionDestroyedException {
			try {
				mSession.join(group);
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
		
		public void leave(GroupEndpoint group) throws SessionDestroyedException {
			try {
				mSession.leave(group);
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
		
		public List<GroupEndpoint> getGroups() throws SessionDestroyedException {
			try {
				return mSession.getGroups();
			} catch (RemoteException e) {
				throw new SessionDestroyedException("remote session error");
			}
		}
	};
	
	/**
	 * This method initialize the DTNClient connection to the DTN service. Call this method in the
	 * onCreate() method of your activity or service.
	 * @param context The current context.
	 * @param reg A registration object containing the application endpoint and all additional subscriptions.
	 * @throws ServiceNotAvailableException is thrown if the DTN service is not available on this device.
	 */
	public synchronized void initialize(Context context, Registration reg) throws ServiceNotAvailableException {
		// set the context
		mContext = context;
		
		// store registration
		mRegistration = reg;
		
		// register to daemon events
		IntentFilter rfilter = new IntentFilter(de.tubs.ibr.dtn.Intent.REGISTRATION);
		rfilter.addCategory(context.getApplicationContext().getPackageName());
		context.registerReceiver(mStateReceiver, rfilter);
		
		// mark this client as initialized
		mInitialized = true;
		
		// Establish a connection with the service.
		reconnect();
	}
	
	public synchronized void reconnect() throws ServiceNotAvailableException {
		if (!mInitialized) return;
		
		// Establish a connection with the service.
		Services.SERVICE_APPLICATION.bind(mContext, mConnection, Context.BIND_AUTO_CREATE);
	}
	
	/**
	 * This method terminates the DTNClient and shutdown all running background tasks. Call this
	 * method in the onDestroy() method of your activity or service.
	 */
	public synchronized void terminate() {
		if (mInitialized) {
			// unregister to daemon events
			mContext.unregisterReceiver(mStateReceiver);
			
			// Detach our existing connection.
			mContext.unbindService(mConnection);
			
			if (mConnected) {
				// set state to disconnected
				mConnected = false;
				
				// announce disconnected state
				mSessionHandler.onSessionDisconnected();
			}
			
			// mark this client as uninitialized
			mInitialized = false;
			
			// free the service
			mService = null;
		}
	}
}
