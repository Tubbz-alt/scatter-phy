import select
import socket
import time
import _thread
import threading
import getopt
import queue
import random
from time import sleep
import signal, os
import struct

import sys
sys.path.append('../../')
sys.path.append('../../communicator/python/')

from communicator.python.Communicator import Message
from communicator.python.LayerCommunicator import LayerCommunicator
import communicator.python.interf_pb2 as interf

# ********************************* Definitions *********************************
# Transport Block Size from 3GPP TS 36.213 v10.3.0 table 7.1.7.2.1-1
# Number of bits (TB) that can be transmitted in one suframe, i.e., 1 ms. These number are for a bandwidth of 5 MHz, i.e., 25 RBs.
NUM_BYTES_PER_SUBFRAME_VS_MCS_1MHz4 = [19,26,32,41,51,63,75,89,101,117,117,129,149,169,193,217,225,225,241,269,293,325,349,373,405,437,453,469,549]
NUM_BYTES_PER_SUBFRAME_VS_MCS_3MHz = [49,65,81,109,133,165,193,225,261,293,293,333,373,421,485,533,573,573,621,669,749,807,871,935,999,1063,1143,1191,1383]
NUM_BYTES_PER_SUBFRAME_VS_MCS_5MHz = [85,113,137,177,225,277,325,389,437,501,501,549,621,717,807,903,967,967,999,1143,1239,1335,1431,1572,1692,1764,1908,1980,2292]
NUM_BYTES_PER_SUBFRAME_VS_MCS_10MHz = [173,225,277,357,453,549,645,775,871,999,999,1095,1239,1431,1620,1764,1908,1908,2052,2292,2481,2673,2865,3182,3422,3542,3822,3963,4587]
NUM_BYTES_PER_SUBFRAME_VS_MCS_15MHz = [261,341,421,549,669,839,967,1143,1335,1479,1479,1620,1908,2124,2385,2673,2865,2865,3062,3422,3662,4107,4395,4736,5072,5477,5669,5861,6882]
NUM_BYTES_PER_SUBFRAME_VS_MCS_20MHz = [349,453,573,717,903,1095,1287,1527,1764,1980,1980,2196,2481,2865,3182,3542,3822,3822,4107,4587,4904,5477,5861,6378,6882,7167,7708,7972,9422]

# PHY Results.
PHY_UNKNOWN             = 0
PHY_SUCCESS             = 100
PHY_ERROR               = 101
PHY_TIMEOUT             = 102
PHY_LBT_TIMEOUT         = 103

# Physical Layer States.
PHY_UNKNOWN_ST          = 0
PHY_RX_ST               = 1
PHY_TX_ST               = 2

# BW Indexes.
BW_UNKNOWN              = 0 # unknown
BW_IDX_OneDotFour	    = 1	# 1.4 MHz
BW_IDX_Three	   		= 2	# 3 MHz
BW_IDX_Five	   		    = 3	# 5 MHz
BW_IDX_Ten		   		= 4	# 10 MHz
BW_IDX_Fifteen          = 5 # 15 MHz
BW_IDX_Twenty           = 6 # 20 MHz

# ************ Functions ************
tx_exit_flag = False
def handler(signum, frame):
    global tx_exit_flag
    tx_exit_flag = True

def getExitFlag():
    global tx_exit_flag
    return tx_exit_flag

tx_stat_flag_lock = threading.Lock()
is_tx_stat_received = False
def get_is_stat_received():
    tx_stat_flag_lock.acquire()
    global is_tx_stat_received
    flag = is_tx_stat_received
    tx_stat_flag_lock.release()
    return flag

def set_is_stat_received(flag):
    tx_stat_flag_lock.acquire()
    global is_tx_stat_received
    is_tx_stat_received = flag
    tx_stat_flag_lock.release()

txStatthreadLock = threading.Lock()
tx_sequence_number = 0
def set_seq_number(seq_num):
    # Get lock to synchronize threads
    txStatthreadLock.acquire()
    global tx_sequence_number
    tx_sequence_number = seq_num
    # Free lock to release next thread
    txStatthreadLock.release()

def get_seq_number():
    # Get lock to synchronize threads
    txStatthreadLock.acquire()
    global tx_sequence_number
    seq_num = tx_sequence_number
    # Free lock to release next thread
    txStatthreadLock.release()
    return seq_num

def help():
    print("Usage: pyhton3 continuous_transmissionv2.py")

def printPhyTxStat(internal):
    print("************************ TX PHY Stats Packet ************************")
    print("seq_number: ",internal.transaction_index)
    print("status: ",internal.sendr.result)
    print("host_timestamp: ",internal.sendr.phy_stat.host_timestamp)
    print("frame: ",internal.sendr.phy_stat.frame)
    print("slot: ",internal.sendr.phy_stat.slot)
    print("ch: ",internal.sendr.phy_stat.ch)
    print("mcs: ",internal.sendr.phy_stat.mcs)
    print("Slots to be transmitted: ",internal.sendr.phy_stat.num_cb_total)
    print("Dropped slots: ",internal.sendr.phy_stat.num_cb_err)
    print("power: ",internal.sendr.phy_stat.tx_stat.power)
    print("channel_free_cnt: ",internal.sendr.phy_stat.tx_stat.channel_free_cnt)
    print("channel_busy_cnt: ",internal.sendr.phy_stat.tx_stat.channel_busy_cnt)
    print("free_energy: ",internal.sendr.phy_stat.tx_stat.free_energy)
    print("busy_energy: ",internal.sendr.phy_stat.tx_stat.busy_energy)
    print("Total number of dropped slots: ",internal.sendr.phy_stat.tx_stat.total_dropped_slots)
    print("Coding time: ",internal.sendr.phy_stat.tx_stat.coding_time)
    print("RF boost: ",internal.sendr.phy_stat.tx_stat.rf_boost)
    print("*********************************************************************\n")

def printPhyRxStat(internal):
    print("***************** RX PHY Stats Packet ****************")
    print("seq_number: ",internal.transaction_index)
    print("status: ",internal.receiver.result)
    print("host_timestamp: ",internal.receiver.stat.host_timestamp)
    print("frame: ",internal.receiver.stat.frame)
    print("slot: ",internal.receiver.stat.slot)
    print("ch: ",internal.receiver.stat.ch)
    print("mcs: ",internal.receiver.stat.mcs)
    print("num_cb_total: ",internal.receiver.stat.num_cb_total)
    print("num_cb_err: ",internal.receiver.stat.num_cb_err)
    print("gain: ",internal.receiver.stat.rx_stat.gain)
    print("cqi: ",internal.receiver.stat.rx_stat.cqi)
    print("rssi: ",internal.receiver.stat.rx_stat.rssi)
    print("rsrp: ",internal.receiver.stat.rx_stat.rsrp)
    print("rsrq: ",internal.receiver.stat.rx_stat.rsrq)
    print("sinr: ",internal.receiver.stat.rx_stat.sinr)
    print("detection_errors: ",internal.receiver.stat.rx_stat.detection_errors)
    print("decoding_errors: ",internal.receiver.stat.rx_stat.decoding_errors)
    print("peak_value: ",internal.receiver.stat.rx_stat.peak_value)
    print("noise: ",internal.receiver.stat.rx_stat.noise)
    print("decoded_cfi: ",internal.receiver.stat.rx_stat.decoded_cfi)
    print("found_dci: ",internal.receiver.stat.rx_stat.found_dci)
    print("last_noi: ",internal.receiver.stat.rx_stat.last_noi)
    print("total_packets_synchronized: ",internal.receiver.stat.rx_stat.total_packets_synchronized)
    if(len(internal.receiver.data) > 0):
        print("length: ",internal.receiver.stat.rx_stat.length)
        data = []
        for d in internal.receiver.data:
            data.append(int(d))
        print("data: ",data)
    print("*********************************************************************\n")

def printBasicControl(basic_control, seq_number, tx_data):
    print("******************** Basic Control CMD Transmitted ******************")
    print("trx_flag: ",basic_control.trx_flag)
    print("seq_number: ",seq_number)
    print("bw_index: ",basic_control.bw_index)
    print("ch: ",basic_control.ch)
    print("slot: ",basic_control.slot)
    print("mcs: ",basic_control.mcs)
    print("gain: ",basic_control.gain)
    print("length: ",basic_control.length)
    if(basic_control.trx_flag == 1):
        data = []
        for d in tx_data:
            data.append(int(d))
        print("data: ",data)
    print("*********************************************************************\n")

def inputOptions(argv):
    debug = False # by default debug is enabled.
    profiling = False # by default profiling is disabled.
    single = False # By default we use two computers to run the tests.
    wait_for_tx_stats = False
    send_to_id = 0 # By defaylt we set the ID of the radio where to send a packet to to 0.
    nof_prb = 25 # By dafult we set the number of resource blocks to 25, i.e., 5 MHz bandwidth.
    mcs = 0 # By default MCS is set to 0, the most robust MCS.
    txgain = 0 # By default TX gain is set to 0.
    rxgain = 10 # By default RX gain is set to 10.
    tx_slots = 1
    rx_slots = 1
    tx_channel = 0
    rx_channel = 0
    debugStats = False
    rfboost = 0.0
    num_of_packets_to_tx = -1
    saveStatsToFile = False
    enable_random_packet = True
    threshold_mcs = 24
    maximum_mcs = 28
    ctedata = False

    try:
        opts, args = getopt.getopt(argv,"hdpswi:b:m:t:r:",["help","debug","profile","single","waittxstats","sendtoid=","bw=","mcs=","txgain=","rxgain=","txslots=","rxslots=","txchannel=","rxchannel=","stats","rfboost=","numpktstotx=","savetofile","disablerandompkt","threshmcs=","maxmcs=","ctedata"])
    except getopt.GetoptError:
        help()
        sys.exit(2)
    for opt, arg in opts:
        if opt in ("-h", "--help"):
            help()
            sys.exit()
        elif opt in ("-d", "--debug"):
            debug = True
        elif opt in ("-p", "--profile"):
            profiling = True
        elif opt in ("-s", "--single"):
            single = True
        elif opt in ("-w", "--waittxstats"):
            wait_for_tx_stats = True
        elif opt in ("-i", "--sendtoid"):
            send_to_id = int(arg)
        elif opt in ("-b", "--bw"):
            nof_prb = int(arg)
        elif opt in ("-m", "--mcs"):
            mcs = int(arg)
        elif opt in ("-t", "--txgain"):
            txgain = int(arg)
        elif opt in ("-r", "--rxgain"):
            rxgain = int(arg)
        elif opt in ("--txslots"):
            tx_slots = int(arg)
        elif opt in ("--rxslots"):
            rx_slots = int(arg)
        elif opt in ("--txchannel"):
            tx_channel = int(arg)
        elif opt in ("--rxchannel"):
            rx_channel = int(arg)
        elif opt in ("--stats"):
            debugStats = True
        elif opt in ("--rfboost"):
            rfboost = float(arg)
        elif opt in ("--numpktstotx"):
            num_of_packets_to_tx = int(arg)
        elif opt in ("--savetofile"):
            saveStatsToFile = True
        elif opt in ("--disablerandompkt"):
            enable_random_packet = False
        elif opt in ("--threshmcs"):
            threshold_mcs = int(arg)
        elif opt in ("--maxmcs"):
            maximum_mcs = int(arg)
        elif opt in ("--ctedata"):
            ctedata = True

    return debug, profiling, single, wait_for_tx_stats, send_to_id, nof_prb, mcs, txgain, rxgain, tx_slots, rx_slots, tx_channel, rx_channel, debugStats, rfboost, num_of_packets_to_tx, saveStatsToFile, enable_random_packet, threshold_mcs, maximum_mcs, ctedata

def sendRxCtrlToPhy(lc, bandwidth, chan, mcs_idx, rx_gain, num_data_slots, seq_number, send_to_id, module, debug):

    # Basic Control
    trx_flag    = PHY_RX_ST         # TRX Mode. 1 TX - 0 RX;
    send_to     = send_to_id        # Radio ID to send slot to.
    seq_number  = seq_number        # Sequence number.
    bw_idx      = bandwidth         # By default use BW: 5 MHz. Possible values: 0 - 1.4 MHz, 1 - 3 MHz, 2 - 5 MHz, 3 - 10 MHz.
    mcs         = mcs_idx           # It has no meaning for RX. MCS is recognized automatically by receiver. MCS varies from 0 to 28.
    channel     = chan              # By default use channel 0.
    slot        = 0                 # Slot number (not used now, for future use)
    frame       = 0
    gain        = rx_gain           # RX gain. We use -1 for AGC mode.
    num_slots   = num_data_slots    # Number of slots. How many data slots we expect to receive from peer.

    # Create an Internal message for RX procedure.
    internal = interf.Internal()
    # Add sequence number to internal message.
    internal.transaction_index = seq_number

    # Add values to the Basic Control Message for RX procedure.
    internal.receive.basic_ctrl.trx_flag     = trx_flag
    internal.receive.basic_ctrl.send_to      = send_to
    internal.receive.basic_ctrl.bw_index     = bw_idx
    internal.receive.basic_ctrl.ch           = channel
    internal.receive.basic_ctrl.frame        = frame
    internal.receive.basic_ctrl.slot         = slot
    internal.receive.basic_ctrl.mcs          = mcs
    internal.receive.basic_ctrl.gain         = gain
    internal.receive.basic_ctrl.length       = num_slots

    # Print the basic control structure sent to PHY.
    if(debug == True): printBasicControl(internal.receive.basic_ctrl, internal.transaction_index)

    # Send basic control to PHY.
    if(debug == True): print("Sending basic control to PHY.")
    lc.send(Message(module, interf.MODULE_PHY, internal))

def sendTxCtrlToPhy(lc, chan, bandwidth, mcs_idx, gain_value, slots, sequence_number, send_to_id, module, data, rfboost, length, debug):

    # Basic Control
    trx_flag    = PHY_TX_ST         # TRX Mode. 1 TX - 0 RX; 2
    send_to     = send_to_id        # ID of the radio where to send a packet to.
    seq_number  = sequence_number   # Sequence number.
    bw_idx      = bandwidth         # By default use BW: 5 MHz. Possible values: 0 - 1.4 MHz, 1 - 3 MHz, 2 - 5 MHz, 3 - 10 MHz.
    mcs         = mcs_idx           # It has no meaning for RX. MCS is recognized automatically by receiver. MCS varies from 0 to 28.
    channel     = chan              # By default use channel 0.
    slot        = 0                 # Slot number (not used now, for future use)
    frame       = 0                 # Frame number.
    gain        = gain_value        # RX gain. We use -1 for AGC mode.
    num_slots   = slots             # Number of slots. How many data slots we expect to receive from peer.

    # Create an Internal message for TX procedure.
    internal = interf.Internal()
    # Add sequence number to internal message.
    internal.transaction_index = seq_number
    # Set basic control with proper values.
    internal.send.basic_ctrl.trx_flag     = trx_flag
    internal.send.basic_ctrl.send_to      = send_to
    internal.send.basic_ctrl.bw_index     = bw_idx
    internal.send.basic_ctrl.ch           = channel
    internal.send.basic_ctrl.frame        = frame
    internal.send.basic_ctrl.slot         = slot
    internal.send.basic_ctrl.timestamp    = 0
    internal.send.basic_ctrl.mcs          = mcs
    internal.send.basic_ctrl.gain         = gain
    internal.send.basic_ctrl.rf_boost     = rfboost
    internal.send.basic_ctrl.length       = length
    internal.send.app_data.data           = data

    # Check size of transmited data.
    if(len(internal.send.app_data.data) != internal.send.basic_ctrl.length):
        print("Length of data is diffrent of field length.")
        sys.exit(-1)

    # Print the basic control structure sent to PHY.
    if(debug == True): printBasicControl(internal.send.basic_ctrl, internal.transaction_index, internal.send.app_data.data)

    # Send basic control to PHY.
    lc.send(Message(module, interf.MODULE_PHY, internal))

def generateConstantData(num_slots, bw_idx, mcs, value):
    constant_data = bytes()
    for j in range(num_slots):
        for i in range(getTransportBlockSize(bw_idx, mcs)):
            constant_data = constant_data + bytes([value])
    return constant_data

def generateRandomData(num_slots, bw_idx, mcs):
    random_data = bytes()
    for j in range(num_slots):
        for i in range(getTransportBlockSize(bw_idx, mcs)):
            random_data = random_data + bytes([random.randint(0, 255)])
    return random_data

def generateData(num_slots, seq_number, bw_idx, mcs):
    data = bytes()
    for j in range(num_slots):
        for i in range(getTransportBlockSize(bw_idx, mcs)):
            data = data + bytes([seq_number])
    return data

def generateData2(num_slots, seq_number, bw_idx, mcs):
    data = bytes()
    for j in range(num_slots):
        for i in range(getTransportBlockSize(bw_idx, mcs)):
            data = data + bytes([seq_number+j])
    return data

def generateData3(num_slots, seq_number, bw_idx, mcs):
    data = bytes()
    for j in range(num_slots):
        aux = (seq_number+j)%256
        if((seq_number+j) >= 256):
            aux = aux + 1
        for i in range(getTransportBlockSize(bw_idx, mcs)):
            data = data + bytes([aux])
    return data

def generateData4(num_slots, seq_number, bw_idx, mcs, threshold_mcs, maximum_mcs):
    length = 0
    mcs_current = mcs
    data = bytes()
    for j in range(num_slots):
        if(mcs >= threshold_mcs and j >= 1):
            mcs_current = maximum_mcs
        length = length + getTransportBlockSize(bw_idx, mcs_current)
        aux = (seq_number+j)%256
        if((seq_number+j) >= 256):
            aux = aux + 1
        for i in range(getTransportBlockSize(bw_idx, mcs_current)):
            data = data + bytes([aux])
    return data, length

def generateData5(num_slots, seq_number, bw_idx, mcs, threshold_mcs, maximum_mcs):
    length = 0
    mcs_current = mcs
    for j in range(num_slots):
        if(mcs >= threshold_mcs and j >= 1):
            mcs_current = maximum_mcs
        length = length + getTransportBlockSize(bw_idx, mcs_current)
    data_aux = bytearray(length)

    cnt = 0;
    mcs_current = mcs
    for j in range(num_slots):
        if(mcs >= threshold_mcs and j >= 1):
            mcs_current = maximum_mcs
        # Update sequence number.
        aux = (seq_number+j)%256
        if((seq_number+j) >= 256):
            aux = aux + 1
        for i in range(getTransportBlockSize(bw_idx, mcs_current)):
            data_aux[cnt] = aux
        cnt = cnt + 1;

    data = bytes(data_aux)

    return data, length

def getBWIndex(nof_prb):
    if nof_prb == 6:
        bw_idx = BW_IDX_OneDotFour
    elif nof_prb == 15:
        bw_idx = BW_IDX_Three
    elif nof_prb == 25:
        bw_idx = BW_IDX_Five
    elif nof_prb == 50:
        bw_idx = BW_IDX_Ten
    elif nof_prb == 75:
        bw_idx = BW_IDX_Fifteen
    elif nof_prb == 100:
        bw_idx = BW_IDX_Twenty
    else:
        print("Invalid number of resource blocks.")
        sys.exit(-1)

    return bw_idx

def getTransportBlockSize(index, mcs):
    if index == BW_IDX_OneDotFour:
        tb_size = NUM_BYTES_PER_SUBFRAME_VS_MCS_1MHz4[mcs]
    elif index == BW_IDX_Three:
        tb_size = NUM_BYTES_PER_SUBFRAME_VS_MCS_3MHz[mcs]
    elif index == BW_IDX_Five:
        tb_size = NUM_BYTES_PER_SUBFRAME_VS_MCS_5MHz[mcs]
    elif index == BW_IDX_Ten:
        tb_size = NUM_BYTES_PER_SUBFRAME_VS_MCS_10MHz[mcs]
    elif index == BW_IDX_Fifteen:
        tb_size = NUM_BYTES_PER_SUBFRAME_VS_MCS_15MHz[mcs]
    elif index == BW_IDX_Twenty:
        tb_size = NUM_BYTES_PER_SUBFRAME_VS_MCS_20MHz[mcs]
    else:
        tb_size = -1;

    return tb_size

def receiveStatisticsFromPhyThread(lc, stats_queue, num_of_tx_slots, wait_for_tx_stats, debug, debugStats, saveStatsToFile):
    expected_rxd_byte = -1 # last received byte
    last_rxd_byte = -1
    out_of_seq_cnt = 0 # out of sequence error counter.
    in_seq_cnt = 0 # In sequence counter.
    total_pkt_number = 0 # Total number of received packets.
    correct_tx_slot_counter = 0
    same_packet_cnt = 0
    avg_counter = 0
    avg_cqi = 0
    avg_rssi = 0
    avg_noise = 0

    if(saveStatsToFile == True):
       filename = "lbt_stats.dat"
       f = open(filename, 'w')

    while(getExitFlag() == False):
        # Check if QUEUE is empty.
        if(getExitFlag() == False and lc.get_low_queue().empty() == False):
            try:
                #Try to get next message without waiting.
                msg = lc.get_low_queue().get_nowait()
                internal = msg.message
                if(internal.sendr.result > 0 and internal.sendr.result == PHY_SUCCESS):
                    if(total_pkt_number%5000 == 0): printPhyTxStat(internal)
                    if(wait_for_tx_stats == True):
                        if(internal.transaction_index >= 100 and internal.transaction_index < 200 and get_seq_number() == internal.transaction_index):
                            stats_queue.put(internal)
                            set_is_stat_received(True)
                        else:
                            print("Sequence number out of sequence, expecting:", get_seq_number(), "and received: ", internal.transaction_index, "exiting.")
                            os._exit(-1)
                elif(internal.receiver.result > 0 and internal.receiver.result == PHY_SUCCESS):
                    if(internal.transaction_index == 66):
                       # Get PHY RX Statistics from PHY.
                       total_rxd_bytes = internal.receiver.data
                       host_timestamp = internal.receiver.stat.host_timestamp

                       if(len(total_rxd_bytes) > 0):
                           # Increment the number of received packets.
                           total_pkt_number = total_pkt_number + 1

                           if(total_pkt_number%50 == 0):
                               print("Total number of received packets: %d" % (total_pkt_number))
                               print("MCS: %d" % (internal.receiver.stat.mcs))
                               print("length: %d" % (internal.receiver.stat.rx_stat.length))

                       if(debugStats == True):
                           if(debug == True): printPhyRxStat(internal)
                           avg_counter = avg_counter + 1
                           avg_cqi = avg_cqi + internal.receiver.stat.rx_stat.cqi
                           avg_rssi = avg_rssi + internal.receiver.stat.rx_stat.rssi
                           avg_noise = avg_noise + internal.receiver.stat.rx_stat.noise
                           if(total_pkt_number%1000 == 0):
                                print("Avg. CQI: %f" % (avg_cqi/avg_counter))
                                print("Avg. RSSI: %f" % (avg_rssi/avg_counter))
                                print("Avg. Noise: %f" % (avg_noise/avg_counter))

                       if(saveStatsToFile == True):
                          f.write("%1.4f\t%d\t%.6f\n" % (internal.receiver.stat.rx_stat.rssi, internal.receiver.stat.rx_stat.cqi, internal.receiver.stat.rx_stat.noise))

                       if(total_pkt_number%1000 == 0):
                          print("total_pkt_number: %d" % (total_pkt_number))

                    else:
                        print("Sequence number out of sequence, expecting:", get_seq_number(), "and received: ", internal.transaction_index, "exiting.")
            except queue.Empty:
                print("QUEUE is empty.");
        # Check is exit flag is set.
        if(getExitFlag() == True):
            print("Finish statistics thread.")
            break

    if(saveStatsToFile == True):
       f.close()

if __name__ == '__main__':

    # Parse any input option.
    debug, profiling, single, wait_for_tx_stats, send_to, nof_prb, mcs_idx, tx_gain, rx_gain, num_of_tx_slots, num_of_rx_slots, tx_channel, rx_channel, debugStats, rfboost, num_of_packets_to_tx, saveStatsToFile, enable_random_packet, threshold_mcs, maximum_mcs, ctedata = inputOptions(sys.argv[1:])

    # Set the signal handler.
    signal.signal(signal.SIGINT, handler)

    # Decides if single or two host PCs.
    if(single == False):
        source_module = interf.MODULE_MAC # Make believe it is the MAC layer sending controls to PHY.
    else:
        source_module = interf.MODULE_DEBUG2

    print("Create CommManager object.")
    lc = LayerCommunicator(source_module, [interf.MODULE_PHY])

    # Create a QUEUE to store data.
    # Create a single input and a single output queue for all threads.
    stats_queue = queue.Queue()

    bandwidth = getBWIndex(nof_prb)
    seq_number = 1
    packet_counter = 0
    data, length = generateData4(num_of_tx_slots, seq_number, bandwidth, mcs_idx, threshold_mcs, maximum_mcs)

    # Start TX statistics thread.
    # Wait for PHY TX Statistics from PHY.
    try:
        thread_id = _thread.start_new_thread( receiveStatisticsFromPhyThread, (lc, stats_queue, num_of_tx_slots, wait_for_tx_stats, debug, debugStats, saveStatsToFile, ) )
    except:
        print("Error: unable to start thread")
        sys.exit(-1)

    # Give the thread some time.
    time.sleep(2)

    # Send RX control information to PHY.
    sendRxCtrlToPhy(lc, bandwidth, rx_channel, mcs_idx, rx_gain, num_of_rx_slots, 66, send_to, source_module, debug)

    while(True):

        if(ctedata == False):
            # Generate data according to sequence number.
            data, length = generateData4(num_of_tx_slots, seq_number, bandwidth, mcs_idx, threshold_mcs, maximum_mcs)

        # Send TX control information to PHY.
        sendTxCtrlToPhy(lc, tx_channel, bandwidth, mcs_idx, tx_gain, num_of_tx_slots, seq_number, send_to, source_module, data, rfboost, length, debug)

        # Increment sequence number.
        aux = (seq_number + num_of_tx_slots)%256
        if((seq_number + num_of_tx_slots) >= 256):
            aux = aux + 1
        seq_number = aux

        # Wait sometime before transmitting again.
        if(wait_for_tx_stats == False):
            #time.sleep(0.001)
            time.sleep(0.0009*num_of_tx_slots)

        if(enable_random_packet == True and packet_counter%15 == 0):
            num_of_tx_slots = random.randint(1, 24);
            print("-----------------> Sending %d slots." % (num_of_tx_slots));

        # Increment packet counter.
        packet_counter = packet_counter + 1

        if(getExitFlag() == True or packet_counter == num_of_packets_to_tx):
            break

    print("Leaving test script in 2 seconds....")
    time.sleep(2)
