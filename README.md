#A backup for a Redis clone written in C, following the roadmap of [CodeCrafters](https://app.codecrafters.io/courses/redis/overview)

## Implemented Features

### Core commands
- SET / GET with optional expiration (EX / PX)
- TYPE
- PING / ECHO

### Data structures
- Strings
- Lists (LPUSH / RPUSH / LPOP / LRANGE / LLEN)
- Sets (basic support)
- Hashes (basic support)

### Blocking operations
- BLPOP (blocking list pop with timeout support)


