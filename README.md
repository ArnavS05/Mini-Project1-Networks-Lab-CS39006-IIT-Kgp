# Mini-Project1-Networks-Lab-CS39006-IIT-Kgp
This was one of my mini projects in the Networks Laboratory (CS39006) at IIT Kharagpur. It is based on emulating end-to-end reliable flow control over unreliable communication channels.



## 1. Results (Average Transmissions per Message, T = 5s)

| p    | msgs generated | msgs sent | msgs sent / msgs generated |
|------|---------------|-----------|-----------------------------|
| 0.05 | 240 | 335 | 1.396 |
| 0.10 | 240 | 336 | 1.400 |
| 0.15 | 240 | 356 | 1.483 |
| 0.20 | 240 | 491 | 2.046 |
| 0.25 | 240 | 574 | 2.392 |
| 0.30 | 240 | 567 | 2.363 |
| 0.35 | 240 | 687 | 2.863 |
| 0.40 | 240 | 785 | 3.271 |
| 0.45 | 240 | 771 | 3.213 |
| 0.50 | 240 | 1100 | 4.583 |

**Test Setup**

- File size: **120 KB**
- Chunk size: **512 bytes**
- Total chunks/messages: **240**

Notes:

- `msgs generated` is printed by **user1** when it exits.
- `msgs sent` is printed by **initksocket** when it exits.

---

# 2. Data Structures

## KTPHeader
Header sent with every packet.

- **type** — message type  
  - `DATA (1)`
  - `ACK (2)`
  - `DUPACK (3)`
- **seq** — sequence number (DATA) or last in-order ACK
- **rwnd** — piggybacked receiver window size

---

## KTPMessage
Full packet.

- **hdr** — `KTPHeader`
- **data** — 512-byte payload

---

## SWnd 
Sender Window State.

- **size** — current window size (from piggybacked `rwnd`)
- **seq[]** — sequence numbers of sent but unacked messages
- **count** — number of unacked messages
- **last_send_time** — timestamp of last send

---

## RWnd 
Receiver Window State

- **size** — number of free slots in receive buffer
- **base** — next expected sequence number (for in-order delivery)

---

## KTPSocketEntry
Represents one socket entry in shared memory.

- **is_free** — 1 if slot is available
- **pid** — PID of the owning process
- **udp_sockfd** — underlying UDP socket FD (created by `initksocket`)
- **src_addr** — local address
- **dst_addr** — remote address
- **bound** — 1 after actual UDP `bind()` succeeds
- **bind_requested** — set by `k_bind()`, cleared by `initksocket`
- **close_requested** — set by `k_close()`, cleared by `initksocket`
- **send_buf[][]** — circular buffer of outgoing payloads
- **send_buf_head / tail / count** — circular buffer state
- **recv_buf[][]** — received payloads (out-of-order allowed)
- **recv_buf_valid[]** — indicates occupied slot
- **recv_buf_seq[]** — sequence number stored in each slot
- **recv_buf_count** — number of occupied slots
- **swnd** — sender window
- **rwnd** — receiver window
- **last_ack_seq** — last in-order acknowledged sequence
- **nospace** — set when receive buffer is full
- **next_seq** — next sequence number for transmission

---

## SharedMem

Entire shared memory region.

- **sock[]** — array of `MAX_KTP_SOCKETS` `KTPSocketEntry` structures

---

# 3. Functions

## ksocket.c

### k_socket
- Finds a free slot in shared memory.
- Initializes per-socket state.
- Returns socket pseudo-descriptor (`slot + 1000`).

### k_bind
- Writes source and destination addresses to shared memory.
- Sets `bind_requested = 1`.
- Spin-waits until `initksocket` performs the actual bind.

### k_sendto
- Copies payload into send buffer circular queue.

### k_recvfrom
- Copies in-order message from receive buffer to user.
- Advances `rwnd.base`.

### k_close
- Sets `close_requested = 1`.
- `initksocket` closes the UDP socket and frees the slot.

### k_pending
Returns:

```

send_buf_count + swnd.count

```

If `0`, all data has been acknowledged.

---

## initksocket.c

### thread_R (Receiver Thread)

At each iteration:

1. Processes pending:
   - `bind_requested`
   - `close_requested`

2. Performs actual:
   - `socket()`
   - `bind()`
   - `close()`

3. Runs `select()` loop over all bound sockets  
   with timeout **T/2**.

On receiving packets:

- **DATA**
  - Validate sequence/window
  - Buffer payload
  - Send ACK

- **ACK**
  - Slide sender window

- **DUPACK**
  - Retransmit if needed

---

### thread_S (Sender Thread)

Runs every **T/2** seconds:

- Retransmits unacknowledged packets on timeout
- Sends new messages from send buffer if window allows

---

### garbage_collector

Runs every **5 seconds**.

Checks if owning process is alive.

If not:

- sets `close_requested = 1`
- `thread_R` closes the UDP FD safely.

---

# 4. Bug and Fix

## Bug

When the receive buffer becomes full:

1. Receiver sends **ACK with rwnd = 0**
2. Sender stops sending
3. Application frees buffer using `k_recvfrom`
4. Receiver sends **DUPACK** to wake sender

If this **DUPACK is dropped**:

- sender remains blocked
- receiver stops sending further DUPACKs
- system enters **permanent deadlock**

---

## Fix

The **`nospace` flag is not cleared when DUPACK is sent**.

Behavior:

- As long as `nospace = 1` and space exists,
- `thread_R` keeps sending DUPACK **every T/2**

`nospace` is cleared **only when a new DATA packet arrives**, confirming the sender resumed.

---

# 5. Build

From project root:

```

make

```

Or build individually:

```

make -C lib
make -C init
make -C users

```

Outputs:

- `lib/libksocket.a`
- `init/initksocket`
- `users/user1`
- `users/user2`

---

# 6. Run

## Basic Run (1 Sender–Receiver Pair)

Open **3 terminals**:

### Terminal 1
```

./init/initksocket

```

### Terminal 2
```

./users/user2 127.0.0.1 5001 127.0.0.1 5000 received.bin

```

### Terminal 3
```

./users/user1 127.0.0.1 5000 127.0.0.1 5001 testfile.bin

```

---

## Run 3 Sender–Receiver Pairs

Start `initksocket` once.

### Terminal 1
```

./init/initksocket

```

### Receiver Terminals

```

./users/user2 127.0.0.1 5001 127.0.0.1 5000 recv1.bin
./users/user2 127.0.0.1 5003 127.0.0.1 5002 recv2.bin
./users/user2 127.0.0.1 5005 127.0.0.1 5004 recv3.bin

```

### Sender Terminals

```

./users/user1 127.0.0.1 5000 127.0.0.1 5001 file1.bin
./users/user1 127.0.0.1 5002 127.0.0.1 5003 file2.bin
./users/user1 127.0.0.1 5004 127.0.0.1 5005 file3.bin

```

---

# 7. File Integrity Check

After transfer completes, to check if the file was correctly transferred, run:

```

diff testfile.bin received.bin

```

No output means files are identical.

---

# 8. Shared Memory Cleanup

If `initksocket` exits with:

```

shmget: Invalid argument

```

Clean IPC objects:

```

ipcs -m | awk 'NR>3 && $1!="" {print $2}' | xargs -r ipcrm -m
ipcs -s | awk 'NR>3 && $1!="" {print $2}' | xargs -r ipcrm -s

```
