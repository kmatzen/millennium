# Lock Ordering (#124)

To avoid deadlocks when acquiring multiple locks, follow this order:

1. **daemon_state_mutex** (daemon.c) – daemon state
2. **websocket_mutex** (web_server.c) – WebSocket connection list
3. **event_queue_mutex** (millennium_sdk.c) – event queue
4. **metrics_mutex** (metrics.c) – metrics data
5. **logger_mutex** (logger.c) – logging
6. **g_sip_mutex** (millennium_sdk.c) – SIP registration state

Never hold a lower-numbered lock while acquiring a higher-numbered one.
When in doubt, release all locks before calling into other modules.
