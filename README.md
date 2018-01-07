# TCP Diagnostic Tool Design

## Background

- Linux implements a variety of **C**ongestion **C**ontrol algorithms (Vegas, Cubic, BBR, etc.) for users to choose from. It may be possible to design diagnostic logic for each CC, however, this is extremely cumbersome.
- Some large internet companies will optimize their own server TCP stack so the behavior may not be categorized into any of the known CC. If your diagnostic logic relies on certain CC behavior, you will fail.


- Despite CC's complexity, they still have same behavior in some degree. For example, any CC has slow start (BBR's startup, Cubic's hystart, etc.) phase. We can rely on this, but we can't do more assumptions.

The diagnostic logic I designed was derived from BBR, which is based on a simple network path model. Since the logic has little reliance on the specific CC's behavior, it applies to any CC.

## Network Path Model

The model has two parameters: **bottleneck bandwidth** (BtlBw) and **round-trip propagation delay** (RTprop). From [BBR draft](https://tools.ietf.org/html/draft-cardwell-iccrg-bbr-congestion-control-00#section-3.1):

> BBR is a model-based congestion control algorithm: its behavior is based on an explicit model of the network path over which a transport flow travels. BBR's model includes explicit estimates of two parameters:
>
>       1. BBR.BtlBw: the estimated bottleneck bandwidth available to the transport flow, estimated from the maximum delivery rate sample from a moving window.
>
>       2. BBR.RTprop: the estimated two-way round-trip propagation delay of the path, estimated from the the minimum round-trip delay sample from a moving window.

Obviously, `BtlBw` can be used to diagnose `bandwidth limited`; `RTprop` can be used to diagnose `congestion limited`( high queue delay ). In addition, we can derive an important parameter from this model: **bandwidth-delay product** (BDP), which is then maximum amount of data on the network circuit at any given time. We will see later that it can be used to diagnose `sender limited`.

## Estimate RTprop

`RTprop` estimating is based on `RTT`, which is contiguously measured.  `RTT` samples tend to be above the round-trip propagation delay of the  path, due to "noise" introduced by random variation in physical   transmission processes (e.g. radio link layer noise), queues along the network path, the receiver's delayed ack strategy, ack aggregation, etc.  Thus to filter out these effects we uses a min filter: using the minimum recent RTT sample seen by the connection over that past **several seconds**.

In my implemetation, the time interval is **10 seconds**.

## Estimate BtlBw

`BtlBw` estimating is based on `delivery rate`, which is contiguously measured, like `RTT`. A delivery rate sample records the estimated rate at which the network delivered packets for a single flow, calculated over the time interval between the transmission of a data packet and the acknowledgment of that packet.  Since the rate samples only include packets actually cumulatively and/or selectively acknowledged, the sender knows the exact bytes that were delivered to the receiver. Details of measuring `delivery rate` is [here](https://tools.ietf.org/html/draft-cheng-iccrg-delivery-rate-estimation-00#page-3).

Delivery rate samples tend to be below the bottleneck bandwidth available to the flow, due to "noise" introduced by random variation in physical transmission processes or queues along the network path.  Thus to filter out these effects we uses a max filter: using the windowed maximum recent delivery rate sample seen by the connection over that past **several RTT**. Note that:

- When the TCP flow is sender limited or receiver limited, we can not use the delivery rate to estimate the  `BtlBw`.
- Short-lived (never leaves slow start) or interactive (always send small packets) TCP flow may never fully utilizes the network, leading to underestimation of `BtlBw`. In this case we can't use `BtlBw` to diagnose a TCP flow.

In my implementation, the time window is 10 RTT.

## Identify Slow Start

We all know that BBR’s Startup and CUBIC’s slow start both explore the bottleneck capacity exponentially, doubling their sending rate each round. In order to recognize this pattern, we first **group the packets into flights,** then test whether consecutive flight size fits the exponential relationship. In order to group packets into flights, we need to keep track following variables for each TCP flow:

- **round_trip_count**: round trip counter starts from 1, which corresponds to TCP's three-way handshake


- **last_ack_sequence**: largest ack sequence ever seen.
- **next_send_sequence**: largest date sequence ever seen plus data length of that packet
- **current_round_trip_end**: `next_send_sequence` of previous round trip
- **last_flight_size**:  last flight size in bytes

Upon receiving an ACK, we do the following:

```C++
func calc_flight_size(ack):
    if (ack.sequence > last_ack_sequence)
        last_ack_sequence = ack.sequence
    if (last_ack_sequence > current_round_trip_end):
        round_trip_count++
        int this_flight_size = next_send_sequence - current_round_trip_end
        if (this_flight_size < last_flight_size * 3 / 2):
            is_slow_start = false
        last_flight_size = this_flight_size
        current_round_trip_end = next_send_sequence
```

The above code also describes when to quit slow start phase. In order to improve accuracy, we may need to track more consecutive flight sizes rather than two.

## Identify Receiver Limited

`Receiver limited` means the network bandwidth utilization is limited by the receiver’s advertised window.  To identify this, we need:

- **pipe_size**: currently sent but not acked data length in bytes
- **rwnd**: rwnd of newest ack
- **WSC**: TCP window scale option. *Continuous estimation is needed if we missed receiver's SYN packet*


Upon sending a packet, we do the following:

```C++
func mark_receiver_limited(packet):
    if (pipe_size > (rwnd << wsc) * 9 / 10)
        packet.is_receiver_limited = true
```
Note that receiver may advertises a window size of 0, forcing sender to stop, which gives us no chance to output a diagnosis. However, TCP's **persist timer** saves us. From wikipedia:

> When a receiver advertises a window size of 0, the sender stops sending data and starts the *persist timer*. The persist timer is used to protect TCP from a deadlock situation that could arise if a subsequent window size update from the receiver is lost, and the sender cannot send more data until receiving a new window size update from the receiver. When the persist timer expires, the TCP sender attempts recovery by sending a small packet so that the receiver responds by sending another acknowledgement containing the new window size.

Persist timer force receiver to send acks, which leads to `receiver limited`.

## Identify Sender Limited

`Sender limited` means the network bandwidth utilization is limited either by sender's application (`application limited`) or by sender's OS kernel (`kernel limited`). To indentify this, we need:

- **pipe_size**: currently sent but not acked data length in bytes
- **MSS**: TCP max segment size. *Continuous estimation is needed if we missed receiver's SYN packet*
- **network BDP**: `BtlBw` and `RTprop` product

Upon sending a packet, we do the following:

```c++
func mark_sender_limited(packet):
    if (packet.length < mss)
        packet.is_application_limited = true
    else if (pipe_size < bdp * 4 / 5)
        packet.is_kernel_limited = true
```
TCP Nagle algorithm can affect the accuracy of the diagnosis because it tries to merge small TCP packets into a single `MSS` size packet, leading to `kernel limited` instead of `application limited`. The RFC defines the algorithm as:

> inhibit the sending of new TCP segments when new outgoing data arrives from the user if any previously transmitted data on the connection remains unacknowledged.

However, **round trip level diagnosis** saves us: if at least one small packet is seen in current round trip, we conclude `application limited` rather than `kernel limited`.

## Identify Bandwidth Limited

`Bandwidth limited` means  the sender fully utilizes, and is limited by, the bandwidth on the bottleneck link. To identify this, we need:

- **BtlBw**: bottleneck bandwidth of network path

Upon receiving an ack, we do the following:

```c++
func bandwidth_limited(ack):
    if (ack.delivery_rate >= BtlBw * 4 / 5)
        return true
    return false
```
## Identify Congestion Limited

`Congestion limited` means the network has been congested, **not the sender thinks the network has been congested**. To identify this, we need:

- **RTprop**: round-trip propagation delay

Upon receiving an ack, we do the following:

```c++
func congestion_limited(ack):
    if (ack.rtt > RTprop * 6 / 5)
        return true
    return false
```

Note that we use **queue delay** as a signal of congestion rather than packet loss. We encounter a fundamental measurement ambiguity: whether a measured RTT increase is caused by a path length change, bottleneck bandwidth decrease, or real queuing delay increase from another connection’s traffic. We solve this ambiguity based on following observation: **network path changes happen on time scales >> RTprop**. We simply set a timeout for `RTprop` and restart the measurement once there is no update within this time period.

## Identify Idle TCP Flow

A TCP flow is idle means it has no data to transmit, which gives us no chance to output a diagnosis. However, TCP's/application's **keepalive timer** saves us. From wikipedia:

> A keepalive signal is often sent at predefined intervals, and plays an important role on the Internet. After a signal is sent, if no reply is received the link is assumed to be down and future data will be routed via another path until the link is up again. A keepalive signal can also be used to indicate to Internet infrastructure that the connection should be preserved. Without a keepalive signal, intermediate NAT-enabled routers can drop the connection after timeout.

Keepalive timer force receiver to send acks, which leads to `application limited`.

## Identify Timeout/Unresponsive TCP Flow

A TCP flow is timeout/unresponsive means the network/receiver is down or severely congested, leaving nothing to be acked,  which gives us no chance to output a diagnosis.  However, TCP's **timeout retransmission** saves us. To identify this, we simply output `timeout rexmit` upon a timout retransmission.

## Handle TCP SACK Option

// TODO

**Ingoring SACK can result in overestimation of `BtlBw`.**

## Diagnostic Logic

Upon receiving an ack, we do a diagnosis:

```c++
// pkt - data packet sent
// ack - the corresponding ack

func diagnose(rexmit):
    put("timeout rexmit")
      
func diagnose(pkt, ack):
    if (pkt.is_slow_start)
        put("slow start")
    else if (pkt.is_receiver_limited)
        put("receiver limited")
    else if (pkt.is_application_limited)
        put("application limited")
    else if (pkt.is_kernel_limited)
        put("kernel limited")
    else if (bandwidth_limited(ack))
        put("bandwidth limited")
    else if (congestion_limited(ack))
        put("congetion limited")
    else
        put("unknown limited")
```
**Diagnostic Granularity**. The above code output a diagnostic result for each \{ packet, ack \} pair, which is too granular, and may have a lot of noise. Optional granularity may be fixed number of packets or fixed time interval , just like `Trat`.  However, I use `round trip` level granularity based on following obeservation: **packets in the same round trip tend to suffer from the same limitation. In order to filter out the "noise" or unknown limited, we choose the most votes in the batch of diagnosis results**. Note that the number of packets and time interval may vary in different `round trip`.

## Example

output format:

- round trip counter
- BtlBw
- RTprop
- start time -> end time
- limitation
- acks diagnosed as above limitation / acks received

**file upload** (bulk data transfer):

```
[1] 0kB/s 6112us 07:18:00.674043 -> 07:18:00.680528 [slow start] (1/1)
[2] 1906kB/s 6112us 07:18:00.680528 -> 07:18:00.687249 [slow start] (9/9)
[3] 4034kB/s 6112us 07:18:00.687249 -> 07:18:00.693552 [slow start] (20/20)
[4] 6883kB/s 6112us 07:18:00.693552 -> 07:18:00.699960 [slow start] (34/34)
[5] 9884kB/s 6112us 07:18:00.699960 -> 07:18:00.706513 [slow start] (24/24)
[6] 12237kB/s 6112us 07:18:00.706513 -> 07:18:00.712898 [bandwidth limited] (29/31)
[7] 13414kB/s 6112us 07:18:00.712898 -> 07:18:00.720109 [bandwidth limited] (37/45)
[8] 13649kB/s 6112us 07:18:00.720109 -> 07:18:00.727585 [bandwidth limited] (29/30)
[9] 13649kB/s 6112us 07:18:00.727585 -> 07:18:00.734572 [bandwidth limited] (28/28)
[10] 13884kB/s 6112us 07:18:00.734572 -> 07:18:00.741791 [bandwidth limited] (29/29)
```
**ssh** (interactive data transfer):

```
[1] 0kB/s 5012us 09:11:28.457792 -> 09:11:30.249016 [slow start] (1/1)
[2] 1kB/s 4831us 09:11:30.249016 -> 09:11:30.925888 [application limited] (1/1)
[3] 1kB/s 4824us 09:11:30.925888 -> 09:11:31.516343 [application limited] (1/1)
[4] 1kB/s 4580us 09:11:31.516343 -> 09:11:32.528395 [application limited] (1/1)
[5] 1kB/s 4580us 09:11:32.528395 -> 09:11:32.689936 [application limited] (1/1)
[6] 1kB/s 4573us 09:11:32.689936 -> 09:11:32.795682 [application limited] (1/1)
[7] 1kB/s 4573us 09:11:32.795682 -> 09:11:32.920271 [application limited] (1/1)
[8] 1kB/s 4573us 09:11:32.920271 -> 09:11:33.067761 [application limited] (1/1)
[9] 1kB/s 4573us 09:11:33.067761 -> 09:11:33.185697 [application limited] (1/1)
[10] 1kB/s 4573us 09:11:33.185697 -> 09:11:33.308508 [application limited] (1/1)
[11] 1kB/s 4573us 09:11:33.308508 -> 09:11:33.406548 [application limited] (1/1)
[12] 1kB/s 4573us 09:11:33.406548 -> 09:11:33.536439 [application limited] (1/1)
[13] 1kB/s 4573us 09:11:33.536439 -> 09:11:33.646821 [application limited] (1/1)
[14] 1kB/s 4573us 09:11:33.646821 -> 09:11:33.767388 [application limited] (1/1)
[15] 1kB/s 4573us 09:11:33.767388 -> 09:11:33.892053 [application limited] (1/1)
```

## Weakness

- must run at sender side, or close to sender
- must see both data and ack packet

## Contribution

- real-time fine-grained TCP diagnosis with 7 kinds of results/limitations
- applies to any CC