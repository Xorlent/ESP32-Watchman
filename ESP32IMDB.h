/*
 * ESP32IMDB - Simple In-Memory Database Engine for ESP32
 * 
 * Copyright (c) 2026 Xorlent
 * Licensed under the MIT License. See LICENSE file in the project root.
 * https://github.com/Xorlent/ESP32IMDB
 * 
 * A lightweight, thread-safe in-memory database with support for:
 * - Signed 32-bit integers
 * - 32-bit floating point numbers
 * - MAC addresses (6 bytes)
 * - String values up to 255 bytes
 * - Epoch dates (Unix timestamps)
 * - Boolean values
 * 
 * Features:
 * - Thread-safe operations using FreeRTOS mutexes
 * - Time-to-live (TTL) support with automatic record expiration
 * - Memory-efficient string compaction
 * - Simple, beginner-friendly API
 * - Configurable heap limit protection
 */

#ifndef ESP32IMDB_H
#define ESP32IMDB_H

// Feature flags - set to 0 to disable
#define IMDB_ENABLE_PERSISTENCE 0  // Enable saveToFile/loadFromFile (requires SPIFFS)

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Configurable minimum available heap (bytes) - operations fail if heap drops below this limit
#define IMDB_MIN_HEAP_BYTES 30000

// Maximum string length
#define IMDB_MAX_STRING_LENGTH 255

// Maximum TTL (30 days in milliseconds)
#define IMDB_MAX_TTL_MS (30UL * 24UL * 60UL * 60UL * 1000UL)

// Data type enumeration
enum IMDBDataType {
  IMDB_TYPE_INT32,      // Signed 32-bit integer
  IMDB_TYPE_MAC,        // MAC address (6 bytes)
  IMDB_TYPE_STRING,     // String up to 255 bytes
  IMDB_TYPE_EPOCH,      // Unix timestamp (32-bit epoch)
  IMDB_TYPE_BOOL,       // Boolean value
  IMDB_TYPE_FLOAT       // 32-bit floating point
};

// Result codes
enum IMDBResult {
  IMDB_OK = 0,
  IMDB_ERROR_OUT_OF_MEMORY,
  IMDB_ERROR_HEAP_LIMIT,
  IMDB_ERROR_TABLE_EXISTS,
  IMDB_ERROR_NO_TABLE,
  IMDB_ERROR_INVALID_TYPE,
  IMDB_ERROR_INVALID_VALUE,
  IMDB_ERROR_COLUMN_COUNT_MISMATCH,
  IMDB_ERROR_COLUMN_NOT_FOUND,
  IMDB_ERROR_INVALID_OPERATION,
  IMDB_ERROR_NO_RECORDS,
  IMDB_ERROR_INVALID_MAC_FORMAT,
#if IMDB_ENABLE_PERSISTENCE
  IMDB_ERROR_FILE_OPEN,
  IMDB_ERROR_FILE_WRITE,
  IMDB_ERROR_FILE_READ,
  IMDB_ERROR_CORRUPT_FILE
#endif
};

// Column definition
struct IMDBColumn {
  char name[32];         // Column name
  IMDBDataType type;     // Data type
};

// Field value union for efficient storage
union IMDBFieldValue {
  int32_t int32Value;
  uint8_t macAddress[6];
  char* stringValue;     // Dynamically allocated, compacted
  uint32_t epochValue;
  bool boolValue;
  float floatValue;
};

// Record structure
struct IMDBRecord {
  IMDBFieldValue* fields;  // Array of field values
  uint32_t expiryMillis;   // Expiry time (0 = no expiry)
  bool isValid;            // Flag for deleted records
};

// Comparison operators for WHERE clauses
enum IMDBOperator {
  IMDB_OP_EQUAL,
  IMDB_OP_NOT_EQUAL,
  IMDB_OP_GREATER,
  IMDB_OP_LESS,
  IMDB_OP_GREATER_EQUAL,
  IMDB_OP_LESS_EQUAL
};

// Math operations for UPDATE
enum IMDBMathOp {
  IMDB_MATH_ADD,
  IMDB_MATH_SUBTRACT,
  IMDB_MATH_MULTIPLY,
  IMDB_MATH_DIVIDE,
  IMDB_MATH_MODULO
};

// Select result structure
struct IMDBSelectResult {
  int32_t int32Value;
  uint8_t macAddress[6];
  char stringValue[IMDB_MAX_STRING_LENGTH + 1];
  uint32_t epochValue;
  bool boolValue;
  float floatValue;
  IMDBDataType type;
  bool hasValue;
};

class ESP32IMDB {
public:
  ESP32IMDB();
  ~ESP32IMDB();

  // Table operations
  IMDBResult createTable(const IMDBColumn* columns, uint8_t columnCount);
  IMDBResult dropTable();
  
  // Data operations
  IMDBResult insert(const void** values, uint32_t ttlMillis = 0);
  IMDBResult update(const char* whereColumn, const void* whereValue, 
                    const char* setColumn, const void* setValue);
  IMDBResult updateWithMath(const char* whereColumn, const void* whereValue,
                           const char* setColumn, IMDBMathOp operation, 
                           int32_t operand);
  IMDBResult deleteRecords(const char* whereColumn, const void* whereValue);
  
  // Query operations
  IMDBResult select(const char* column, const char* whereColumn, 
                   const void* whereValue, IMDBSelectResult* result);
  IMDBResult selectAll(const char* whereColumn, const void* whereValue,
                      IMDBSelectResult** results, int* resultCount);
  
  // Aggregate functions
  int32_t count();
  int32_t countWhere(const char* whereColumn, const void* whereValue);
  IMDBResult min(const char* column, IMDBSelectResult* result);
  IMDBResult max(const char* column, IMDBSelectResult* result);
  IMDBResult top(int n, IMDBSelectResult** results, int* resultCount);
  
  // Utility functions
  void purgeExpiredRecords();
  int getRecordCount() const;
  size_t getMemoryUsage() const;
  bool isThreadSafe() const;
  
#if IMDB_ENABLE_PERSISTENCE
  // Persistence functions
  IMDBResult saveToFile(const char* filename);
  IMDBResult loadFromFile(const char* filename);
#endif
  
  // Helper functions for value preparation
  static bool parseMacAddress(const char* macStr, uint8_t* macBytes);
  static void formatMacAddress(const uint8_t* macBytes, char* output);
  static const char* resultToString(IMDBResult result);

private:
  IMDBColumn* _columns;
  uint8_t _columnCount;
  IMDBRecord* _records;
  int _recordCount;
  int _recordCapacity;
  mutable SemaphoreHandle_t _mutex;  // Mutable for const method locking
  bool _tableExists;
  
  // Internal helper functions
  bool checkHeapLimit() const;
  int findColumnIndex(const char* columnName) const;
  bool compareValues(const IMDBFieldValue* fieldValue, const void* compareValue, 
                    IMDBDataType type, IMDBOperator op = IMDB_OP_EQUAL) const;
  bool isRecordExpired(uint32_t expiryMillis) const;
  IMDBResult growRecordArray();
  void freeRecord(IMDBRecord* record);
  void compactRecords();
  IMDBResult copyFieldValue(IMDBFieldValue* dest, const void* src, IMDBDataType type);
  void getFieldValue(const IMDBFieldValue* field, IMDBDataType type, IMDBSelectResult* result) const;
  
  // Thread-safe lock/unlock wrappers
  void lock() const;
  void unlock() const;
};

#endif // ESP32IMDB_H
