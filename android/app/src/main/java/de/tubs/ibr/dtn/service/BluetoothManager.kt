package de.tubs.ibr.dtn.service

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.LocalSocket
import android.net.LocalSocket.SOCKET_DGRAM
import android.net.LocalSocketAddress
import android.system.Os
import android.util.Log
import java.io.*
import java.util.*
import kotlin.math.min

val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
const val TAG = "BluetoothManager"

class BluetoothManager(val context: Context) {
    private var localSocket: LocalSocketThread? = null
    private var connectedDevice: BluetoothThread? = null
    private var discoveryEnabled: Boolean = false
    private var resumed: Boolean = false

    private val broadcastSocket by lazy(LazyThreadSafetyMode.NONE) {
        DaemonStorageUtils.getUnixSocket(context, "handler.broadcast")
    }
    private val bluetoothBindSocket by lazy(LazyThreadSafetyMode.NONE) {
        DaemonStorageUtils.getUnixSocket(context, "bluetooth")
    }
    private val connectToHandlerSocket by lazy(LazyThreadSafetyMode.NONE) {
        DaemonStorageUtils.getUnixSocket(context, "handler")
    }
    private val bluetoothAdapter by lazy(LazyThreadSafetyMode.NONE) {
        BluetoothAdapter.getDefaultAdapter()
    }

    fun onCreate() {}

    fun onResume() {
        if (resumed) return
        resumed = true

        localSocket = LocalSocketThread(::forwardToBluetoothSocket, bluetoothBindSocket, connectToHandlerSocket)
                .apply { start() }

        File(broadcastSocket).apply {
            if (delete()) Log.d(TAG, "overwriting file $this")
            deleteOnExit()
        }
        Os.symlink(bluetoothBindSocket, broadcastSocket) // broadcasts should go directly to the bound address

        val filter = IntentFilter()
        filter.addAction(BluetoothDevice.ACTION_FOUND)
        filter.addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
        filter.addAction(BluetoothAdapter.ACTION_CONNECTION_STATE_CHANGED)
        context.registerReceiver(receiver, filter)

        if (discoveryEnabled)
            startDiscovery()
    }

    fun onPause() {
        if (!resumed) return
        resumed = false

        context.unregisterReceiver(receiver)
        connectedDevice?.shutdown()
        connectedDevice = null
        localSocket?.shutdown()
        localSocket = null
        File(broadcastSocket).delete()
    }

    fun onDestroy() {}

    fun startDiscovery(): Boolean {
        if (bluetoothAdapter?.isEnabled != true) {
            return false
        }
        discoveryEnabled = true
        if (localSocket?.isOpen() != true) {
            return false // local socket is broken
        }
        if (connectedDevice?.isAlive == true) {
            return true // already connected
        }

        // FIXME cycle through available devices in background thread, retry after some time if not connected
        val device = bluetoothAdapter?.bondedDevices?.firstOrNull(::checkDevice)
        if (device != null) {
            connectDevice(device)
        } else {
            if (bluetoothAdapter?.startDiscovery() != true
                    && bluetoothAdapter?.isDiscovering != true) {
                Log.d(TAG, "couldn't start discovery")
                return false
            }
        }
        return true
    }

    fun stopDiscovery() {
        bluetoothAdapter?.cancelDiscovery()
        discoveryEnabled = false
    }

    private fun connectDevice(device: BluetoothDevice) {
        if (connectedDevice?.isAlive != true) {
            connectedDevice = BluetoothThread(::forwardToLocalSocket, device)
                    .apply { start() }
        }
    }

    private fun forwardToLocalSocket(bytes: ByteArray, len: Int) {
        localSocket?.write(bytes, len = len)
                ?: Log.d(TAG, "discarding ${min(len, bytes.size)} bytes of data " +
                        "as no localSocket is available for writing")
    }

    private fun forwardToBluetoothSocket(bytes: ByteArray, len: Int) {
        connectedDevice?.write(bytes, len = len)
                ?: Log.d(TAG, "discarding ${min(len, bytes.size)} bytes of data " +
                        "as no connectedDevice is available for writing")
    }

    fun checkDevice(device: BluetoothDevice?): Boolean {
        return device?.name == "BiCE SPP Acceptor"
    }

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothDevice.ACTION_FOUND) return
            if (connectedDevice?.isOpen() == true) return
            val device: BluetoothDevice? = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
            if (checkDevice(device))
                connectDevice(device!!)
        }
    }

    private abstract inner class ShovelThread<S : Closeable>(val handler: (ByteArray, Int) -> Unit) : Thread() {
        abstract fun makeSocket(): S
        abstract fun getInputStream(): InputStream?
        abstract fun getOutputStream(): OutputStream?
        abstract fun isOpen(): Boolean

        protected var mmSocket: S? = null

        override fun run() {
            val mmBuffer = ByteArray(1024)
            try {
                makeSocket().use {
                    mmSocket = it
                    val inputStream = getInputStream()!!
                    Log.d(TAG, "$this started ${isOpen()}")
                    while (isOpen()) {
                        val numBytes = inputStream.read(mmBuffer)
                        Log.d(TAG, "$this received $numBytes bytes of data")
                        try {
                            handler(mmBuffer, numBytes)
                        } catch (e: IOException) {
                            Log.d(TAG, "$this could not forward $numBytes bytes of data to handler", e)
                        }
                    }
                }
            } catch (e: IOException) {
                Log.e(TAG, "$this communication failed", e)
            } finally {
                Log.d(TAG, "$this finalized")
                mmSocket = null
            }
        }

        open fun write(data: ByteArray, off: Int = 0, len: Int = Integer.MAX_VALUE) {
            if (isOpen()) {
                val outputStream = getOutputStream()
                if (outputStream != null) {
                    outputStream.write(data, off, len)
                    Log.d(TAG, "$this sent ${min(len, data.size)} bytes of data")
                } else {
                    Log.d(TAG, "$this discarded ${min(len, data.size)} bytes of data " +
                            "as the outputstream to $mmSocket is not available")
                }
            } else {
                Log.d(TAG, "$this discarded ${min(len, data.size)} bytes of data " +
                        "as the connection via $mmSocket is not open")
            }
        }

        open fun shutdown() {
            try {
                mmSocket?.close()
            } catch (e: IOException) {
                Log.e(TAG, "Could not close the client socket", e)
            }
        }
    }


    private inner class LocalSocketThread(handler: (ByteArray, Int) -> Unit, val local: String?, val remote: String?) : ShovelThread<LocalSocket>(handler) {
        override fun makeSocket(): LocalSocket {
            val socket = LocalSocket(SOCKET_DGRAM)
            if (local != null) {
                File(local).apply {
                    if (delete()) Log.d(TAG, "overwriting file $this")
                    deleteOnExit()
                }
                socket.bind(LocalSocketAddress(local, LocalSocketAddress.Namespace.FILESYSTEM))
            }
            return socket
        }

        override fun getInputStream() = mmSocket?.inputStream

        override fun getOutputStream() = mmSocket?.outputStream

        override fun isOpen() = isAlive && mmSocket?.fileDescriptor?.valid() == true

        override fun toString() = "LocalSocketThread{$local, $remote}"

        override fun write(data: ByteArray, off: Int, len: Int) {
            if (remote != null && mmSocket?.isConnected == false) {
                try {
                    mmSocket?.connect(LocalSocketAddress(remote, LocalSocketAddress.Namespace.FILESYSTEM))
                } catch (e: IOException) {
                    Log.d(TAG, "$this discarded ${min(len, data.size)} bytes of data " +
                            "as the Socket $mmSocket could not be connected", e)
                    return
                }
            }
            super.write(data, off, len)
        }

        override fun shutdown() {
            if (mmSocket?.isBound == true && local != null) {
                File(local).delete()
            }
            super.shutdown()
        }
    }

    private inner class BluetoothThread(handler: (ByteArray, Int) -> Unit, val device: BluetoothDevice) : ShovelThread<BluetoothSocket>(handler) {
        override fun makeSocket(): BluetoothSocket {
            bluetoothAdapter?.cancelDiscovery()
            val socket = device.createInsecureRfcommSocketToServiceRecord(SPP_UUID)
            if (!socket.isConnected)
                socket.connect()
            return socket
        }

        override fun getInputStream() = mmSocket?.inputStream

        override fun getOutputStream() = mmSocket?.outputStream

        override fun isOpen() = isAlive && mmSocket?.isConnected == true

        override fun toString() = "BluetoothThread{${device.address} '${device.name}'}"
    }
}
