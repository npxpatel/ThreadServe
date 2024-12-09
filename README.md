# **Multi-Threaded Proxy Server with LRU Cache**

This project is implemented using `C` and a Parsing Library.

---

## **Project Overview**

### **Introduction**
A multithreaded proxy server capable of handling multiple client requests simultaneously. It includes an optional cache system using the LRU algorithm to optimize request handling.

---

### **Key Features**
- **Multithreading**: Utilizes threads to handle multiple client requests simultaneously, improving responsiveness and throughput.
- **LRU Cache Implementation**: Caches frequently accessed web pages to reduce latency and bandwidth usage.
- **Concurrency Control**: Synchronization mechanisms such as semaphores and locks ensure thread-safe operations.

---

### **How Did We Implement Multi-threading?**
- **Semaphore over Condition Variables**:
  - `pthread_join()` and `pthread_exit()` require thread IDs for thread management.
  - Semaphores (`sem_wait()` and `sem_post()`) do not require parameters, making them a better choice for simplicity and concurrency control.

---

### **Motivation**
- **Understanding**:
  - How requests are processed from the local machine to the server.
  - Handling multiple client requests concurrently.
  - Locking mechanisms for ensuring thread safety.
  - The concept of caching and its role in browsers.

---

### **Future Enhancements**
- Extend the proxy server to handle `POST` requests.
- Extend functionality to handle HTTPS traffic using SSL/TLS.

---

## **How to Run**

```bash
$ git clone https://github.com/npxpatel/ThreadServe.git
$ cd ThreadServe
$ make all
$ ./proxy <port no.>
```

---

# Note:
- This code can only be run in Linux Machine. Please disable your browser cache.
- Can run in WSL if required

---

## Contributing

Feel free to add some and extend as you want!
