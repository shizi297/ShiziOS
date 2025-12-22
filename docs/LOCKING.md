# Lock Hierarchy

L1 zone.lock
L2 mem_block.lock

Locking Rules:
Locks MUST be acquired in order from higher to lower hierarchy.
If both L1 and L2 are needed, you MUST acquire L1 before acquiring L2.

Important Notes:
- Reverse acquisition (L2 before L1) is PROHIBITED
- Attempting to acquire a higher-level lock while holding a lower-level lock is NOT ALLOWED
- Locks should be released in the reverse order of acquisition