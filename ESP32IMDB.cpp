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

#include "ESP32IMDB.h"
#include <string.h>
#include <stdlib.h>
#if IMDB_ENABLE_PERSISTENCE
#include <SPIFFS.h>
#endif

// Constructor
ESP32IMDB::ESP32IMDB() {
  _columns = nullptr;
  _columnCount = 0;
  _records = nullptr;
  _recordCount = 0;
  _recordCapacity = 0;
  _tableExists = false;
  _mutex = xSemaphoreCreateMutex();
  // Note: If mutex creation fails, operations will still work but won't be thread-safe.
  // Call isThreadSafe() to check if mutex initialization succeeded.
}

// Destructor
ESP32IMDB::~ESP32IMDB() {
  dropTable();
  if (_mutex != nullptr) {
    vSemaphoreDelete(_mutex);
  }
}

// Thread-safe lock
void ESP32IMDB::lock() const {
  if (_mutex != nullptr) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
  }
}

// Thread-safe unlock
void ESP32IMDB::unlock() const {
  if (_mutex != nullptr) {
    xSemaphoreGive(_mutex);
  }
}

// Check if heap is above minimum limit
bool ESP32IMDB::checkHeapLimit() const {
  return ESP.getFreeHeap() >= IMDB_MIN_HEAP_BYTES;
}

// Create a new table
IMDBResult ESP32IMDB::createTable(const IMDBColumn* columns, uint8_t columnCount) {
  lock();
  
  if (_tableExists) {
    unlock();
    return IMDB_ERROR_TABLE_EXISTS;
  }
  
  if (columns == nullptr || columnCount == 0) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  if (!checkHeapLimit()) {
    unlock();
    return IMDB_ERROR_HEAP_LIMIT;
  }
  
  _columns = (IMDBColumn*)malloc(sizeof(IMDBColumn) * columnCount);
  if (_columns == nullptr) {
    unlock();
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  memcpy(_columns, columns, sizeof(IMDBColumn) * columnCount);
  _columnCount = columnCount;
  _tableExists = true;
  
  // Initial capacity for records
  _recordCapacity = 10;
  _records = (IMDBRecord*)malloc(sizeof(IMDBRecord) * _recordCapacity);
  if (_records == nullptr) {
    free(_columns);
    _columns = nullptr;
    _tableExists = false;
    unlock();
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  _recordCount = 0;
  
  unlock();
  return IMDB_OK;
}

// Drop the table and free all memory
IMDBResult ESP32IMDB::dropTable() {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  // Free all records
  for (int i = 0; i < _recordCount; i++) {
    freeRecord(&_records[i]);
  }
  
  // Free arrays
  free(_records);
  free(_columns);
  
  _records = nullptr;
  _columns = nullptr;
  _recordCount = 0;
  _recordCapacity = 0;
  _columnCount = 0;
  _tableExists = false;
  
  unlock();
  return IMDB_OK;
}

// Free a single record's allocated memory
void ESP32IMDB::freeRecord(IMDBRecord* record) {
  if (record->fields != nullptr) {
    // Free string fields
    if (_columns != nullptr) {
      for (int i = 0; i < _columnCount; i++) {
        if (_columns[i].type == IMDB_TYPE_STRING && record->fields[i].stringValue != nullptr) {
          free(record->fields[i].stringValue);
        }
      }
    }
    free(record->fields);
    record->fields = nullptr;
  }
}

// Find column index by name
int ESP32IMDB::findColumnIndex(const char* columnName) const {
  for (int i = 0; i < _columnCount; i++) {
    if (strcmp(_columns[i].name, columnName) == 0) {
      return i;
    }
  }
  return -1;
}

// Copy field value with proper memory allocation for strings
IMDBResult ESP32IMDB::copyFieldValue(IMDBFieldValue* dest, const void* src, IMDBDataType type) {
  switch (type) {
    case IMDB_TYPE_INT32:
      dest->int32Value = *(const int32_t*)src;
      break;
      
    case IMDB_TYPE_MAC:
      memcpy(dest->macAddress, src, 6);
      break;
      
    case IMDB_TYPE_STRING: {
      const char* srcStr = *(const char**)src;
      if (srcStr == nullptr) {
        return IMDB_ERROR_INVALID_VALUE;
      }
      size_t len = strlen(srcStr);
      if (len > IMDB_MAX_STRING_LENGTH) {
        len = IMDB_MAX_STRING_LENGTH;
      }
      // Allocate only needed space (compacted storage)
      dest->stringValue = (char*)malloc(len + 1);
      if (dest->stringValue == nullptr) {
        return IMDB_ERROR_OUT_OF_MEMORY;
      }
      strncpy(dest->stringValue, srcStr, len);
      dest->stringValue[len] = '\0';
      break;
    }
    
    case IMDB_TYPE_EPOCH:
      dest->epochValue = *(const uint32_t*)src;
      break;
      
    case IMDB_TYPE_BOOL:
      dest->boolValue = *(const bool*)src;
      break;
      
    case IMDB_TYPE_FLOAT:
      dest->floatValue = *(const float*)src;
      break;
  }
  
  return IMDB_OK;
}

// Check if a record is expired
bool ESP32IMDB::isRecordExpired(uint32_t expiryMillis) const {
  if (expiryMillis == 0) {
    return false;  // No expiry set
  }
  
  uint32_t currentMillis = millis();
  
  // Cast to int32_t to handle wraparound
  return (int32_t)(currentMillis - expiryMillis) >= 0;
}

// Purge expired records
void ESP32IMDB::purgeExpiredRecords() {
  lock();
  
  if (!_tableExists) {
    unlock();
    return;
  }
  
  for (int i = 0; i < _recordCount; i++) {
    if (_records[i].isValid && isRecordExpired(_records[i].expiryMillis)) {
      freeRecord(&_records[i]);
      _records[i].isValid = false;
    }
  }
  
  compactRecords();
  
  unlock();
}

// Compact records array by removing invalid entries
void ESP32IMDB::compactRecords() {
  int writeIndex = 0;
  for (int readIndex = 0; readIndex < _recordCount; readIndex++) {
    if (_records[readIndex].isValid) {
      if (writeIndex != readIndex) {
        _records[writeIndex] = _records[readIndex];
        // Clear the old slot to prevent stale pointers
        _records[readIndex].fields = nullptr;
        _records[readIndex].isValid = false;
      }
      writeIndex++;
    }
  }
  _recordCount = writeIndex;
}

// Grow the records array capacity
IMDBResult ESP32IMDB::growRecordArray() {
  if (!checkHeapLimit()) {
    return IMDB_ERROR_HEAP_LIMIT;
  }
  
  // Check for potential overflow
  if (_recordCapacity > INT_MAX / 2) {
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  int newCapacity = _recordCapacity * 2;
  IMDBRecord* newRecords = (IMDBRecord*)realloc(_records, sizeof(IMDBRecord) * newCapacity);
  if (newRecords == nullptr) {
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  _records = newRecords;
  _recordCapacity = newCapacity;
  return IMDB_OK;
}

// Insert a new record
IMDBResult ESP32IMDB::insert(const void** values, uint32_t ttlMillis) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (values == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  if (!checkHeapLimit()) {
    unlock();
    return IMDB_ERROR_HEAP_LIMIT;
  }
  
  // Validate TTL
  if (ttlMillis > IMDB_MAX_TTL_MS) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  // Grow array if needed
  if (_recordCount >= _recordCapacity) {
    IMDBResult result = growRecordArray();
    if (result != IMDB_OK) {
      unlock();
      return result;
    }
  }
  
  // Allocate record fields
  IMDBRecord* record = &_records[_recordCount];
  record->fields = (IMDBFieldValue*)malloc(sizeof(IMDBFieldValue) * _columnCount);
  if (record->fields == nullptr) {
    unlock();
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  // Initialize fields
  memset(record->fields, 0, sizeof(IMDBFieldValue) * _columnCount);
  
  // Copy values
  for (int i = 0; i < _columnCount; i++) {
    if (values[i] == nullptr) {
      // Cleanup on error
      for (int j = 0; j < i; j++) {
        if (_columns[j].type == IMDB_TYPE_STRING && record->fields[j].stringValue != nullptr) {
          free(record->fields[j].stringValue);
        }
      }
      free(record->fields);
      unlock();
      return IMDB_ERROR_INVALID_VALUE;
    }
    
    IMDBResult result = copyFieldValue(&record->fields[i], values[i], _columns[i].type);
    if (result != IMDB_OK) {
      // Cleanup on error
      for (int j = 0; j < i; j++) {
        if (_columns[j].type == IMDB_TYPE_STRING && record->fields[j].stringValue != nullptr) {
          free(record->fields[j].stringValue);
        }
      }
      free(record->fields);
      unlock();
      return result;
    }
  }
  
  // Set expiry TTL
  if (ttlMillis > 0) {
    record->expiryMillis = millis() + ttlMillis;
  } else {
    record->expiryMillis = 0;
  }
  
  record->isValid = true;
  _recordCount++;
  
  unlock();
  return IMDB_OK;
}

// Compare field values
bool ESP32IMDB::compareValues(const IMDBFieldValue* fieldValue, const void* compareValue, 
                             IMDBDataType type, IMDBOperator op) const {
  if (fieldValue == nullptr || compareValue == nullptr) {
    return false;
  }
  
  switch (type) {
    case IMDB_TYPE_INT32: {
      int32_t a = fieldValue->int32Value;
      int32_t b = *(const int32_t*)compareValue;
      switch (op) {
        case IMDB_OP_EQUAL: return a == b;
        case IMDB_OP_NOT_EQUAL: return a != b;
        case IMDB_OP_GREATER: return a > b;
        case IMDB_OP_LESS: return a < b;
        case IMDB_OP_GREATER_EQUAL: return a >= b;
        case IMDB_OP_LESS_EQUAL: return a <= b;
      }
      break;
    }
    
    case IMDB_TYPE_MAC:
      // Only equality supported for MAC
      return memcmp(fieldValue->macAddress, compareValue, 6) == 0;
      
    case IMDB_TYPE_STRING:
      // Only equality supported for strings
      if (fieldValue->stringValue == nullptr || compareValue == nullptr) {
        return false;
      }
      return strcmp(fieldValue->stringValue, *(const char**)compareValue) == 0;
      
    case IMDB_TYPE_EPOCH: {
      uint32_t a = fieldValue->epochValue;
      uint32_t b = *(const uint32_t*)compareValue;
      switch (op) {
        case IMDB_OP_EQUAL: return a == b;
        case IMDB_OP_NOT_EQUAL: return a != b;
        case IMDB_OP_GREATER: return a > b;
        case IMDB_OP_LESS: return a < b;
        case IMDB_OP_GREATER_EQUAL: return a >= b;
        case IMDB_OP_LESS_EQUAL: return a <= b;
      }
      break;
    }
    
    case IMDB_TYPE_BOOL:
      return fieldValue->boolValue == *(const bool*)compareValue;
      
    case IMDB_TYPE_FLOAT: {
      float a = fieldValue->floatValue;
      float b = *(const float*)compareValue;
      switch (op) {
        case IMDB_OP_EQUAL: return a == b;
        case IMDB_OP_NOT_EQUAL: return a != b;
        case IMDB_OP_GREATER: return a > b;
        case IMDB_OP_LESS: return a < b;
        case IMDB_OP_GREATER_EQUAL: return a >= b;
        case IMDB_OP_LESS_EQUAL: return a <= b;
      }
      break;
    }
  }
  
  return false;
}

// Update records matching WHERE condition
IMDBResult ESP32IMDB::update(const char* whereColumn, const void* whereValue,
                            const char* setColumn, const void* setValue) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (whereColumn == nullptr || whereValue == nullptr || setColumn == nullptr || setValue == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  int whereIdx = findColumnIndex(whereColumn);
  int setIdx = findColumnIndex(setColumn);
  
  if (whereIdx < 0 || setIdx < 0) {
    unlock();
    return IMDB_ERROR_COLUMN_NOT_FOUND;
  }
  
  // Update matching records
  bool updated = false;
  for (int i = 0; i < _recordCount; i++) {
    if (!_records[i].isValid || isRecordExpired(_records[i].expiryMillis)) {
      continue;
    }
    
    if (compareValues(&_records[i].fields[whereIdx], whereValue, _columns[whereIdx].type)) {
      // Free old value if string
      if (_columns[setIdx].type == IMDB_TYPE_STRING && _records[i].fields[setIdx].stringValue != nullptr) {
        free(_records[i].fields[setIdx].stringValue);
        _records[i].fields[setIdx].stringValue = nullptr;
      }
      
      // Copy new value
      IMDBResult result = copyFieldValue(&_records[i].fields[setIdx], setValue, _columns[setIdx].type);
      if (result != IMDB_OK) {
        unlock();
        return result;
      }
      updated = true;
    }
  }
  
  unlock();
  return updated ? IMDB_OK : IMDB_ERROR_NO_RECORDS;
}

// Simple float modulo implementation
static inline float floatModulo(float x, float y) {
  if (y == 0.0f) return 0.0f;
  return x - ((int)(x / y)) * y;
}

// Update with math operation
IMDBResult ESP32IMDB::updateWithMath(const char* whereColumn, const void* whereValue,
                                    const char* setColumn, IMDBMathOp operation, 
                                    int32_t operand) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (whereColumn == nullptr || whereValue == nullptr || setColumn == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  int whereIdx = findColumnIndex(whereColumn);
  int setIdx = findColumnIndex(setColumn);
  
  if (whereIdx < 0 || setIdx < 0) {
    unlock();
    return IMDB_ERROR_COLUMN_NOT_FOUND;
  }
  
  // Math operations only supported on INT32, EPOCH, and FLOAT types
  if (_columns[setIdx].type != IMDB_TYPE_INT32 && 
      _columns[setIdx].type != IMDB_TYPE_EPOCH && 
      _columns[setIdx].type != IMDB_TYPE_FLOAT) {
    unlock();
    return IMDB_ERROR_INVALID_TYPE;
  }
  
  // Update matching records
  bool updated = false;
  for (int i = 0; i < _recordCount; i++) {
    if (!_records[i].isValid || isRecordExpired(_records[i].expiryMillis)) {
      continue;
    }
    
    if (compareValues(&_records[i].fields[whereIdx], whereValue, _columns[whereIdx].type)) {
      if (_columns[setIdx].type == IMDB_TYPE_FLOAT) {
        // Float math operations
        float* valuePtr = &_records[i].fields[setIdx].floatValue;
        float floatOperand = (float)operand;
        
        switch (operation) {
          case IMDB_MATH_ADD:
            *valuePtr += floatOperand;
            break;
          case IMDB_MATH_SUBTRACT:
            *valuePtr -= floatOperand;
            break;
          case IMDB_MATH_MULTIPLY:
            *valuePtr *= floatOperand;
            break;
          case IMDB_MATH_DIVIDE:
            if (operand == 0) {
              unlock();
              return IMDB_ERROR_INVALID_OPERATION;
            }
            *valuePtr /= floatOperand;
            break;
          case IMDB_MATH_MODULO:
            if (operand == 0) {
              unlock();
              return IMDB_ERROR_INVALID_OPERATION;
            }
            *valuePtr = floatModulo(*valuePtr, floatOperand);
            break;
        }
      } else {
        // Integer math operations
        int32_t* valuePtr = (_columns[setIdx].type == IMDB_TYPE_INT32) 
                            ? &_records[i].fields[setIdx].int32Value
                            : (int32_t*)&_records[i].fields[setIdx].epochValue;
        
        switch (operation) {
          case IMDB_MATH_ADD:
            *valuePtr += operand;
            break;
          case IMDB_MATH_SUBTRACT:
            *valuePtr -= operand;
            break;
          case IMDB_MATH_MULTIPLY:
            *valuePtr *= operand;
            break;
          case IMDB_MATH_DIVIDE:
            if (operand == 0) {
              unlock();
              return IMDB_ERROR_INVALID_OPERATION;
            }
            *valuePtr /= operand;
            break;
          case IMDB_MATH_MODULO:
            if (operand == 0) {
              unlock();
              return IMDB_ERROR_INVALID_OPERATION;
            }
            *valuePtr %= operand;
            break;
        }
      }
      updated = true;
    }
  }
  
  unlock();
  return updated ? IMDB_OK : IMDB_ERROR_NO_RECORDS;
}

// Delete records matching WHERE condition
IMDBResult ESP32IMDB::deleteRecords(const char* whereColumn, const void* whereValue) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (whereColumn == nullptr || whereValue == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  int whereIdx = findColumnIndex(whereColumn);
  if (whereIdx < 0) {
    unlock();
    return IMDB_ERROR_COLUMN_NOT_FOUND;
  }
  
  bool deleted = false;
  for (int i = 0; i < _recordCount; i++) {
    if (!_records[i].isValid) {
      continue;
    }
    
    if (compareValues(&_records[i].fields[whereIdx], whereValue, _columns[whereIdx].type)) {
      freeRecord(&_records[i]);
      _records[i].isValid = false;
      deleted = true;
    }
  }
  
  if (deleted) {
    compactRecords();
  }
  
  unlock();
  return deleted ? IMDB_OK : IMDB_ERROR_NO_RECORDS;
}

// Copy field value to result structure
void ESP32IMDB::getFieldValue(const IMDBFieldValue* field, IMDBDataType type, 
                             IMDBSelectResult* result) const {
  result->type = type;
  result->hasValue = true;
  
  switch (type) {
    case IMDB_TYPE_INT32:
      result->int32Value = field->int32Value;
      break;
    case IMDB_TYPE_MAC:
      memcpy(result->macAddress, field->macAddress, 6);
      break;
    case IMDB_TYPE_STRING:
      if (field->stringValue != nullptr) {
        strncpy(result->stringValue, field->stringValue, IMDB_MAX_STRING_LENGTH);
        result->stringValue[IMDB_MAX_STRING_LENGTH] = '\0';
      } else {
        result->stringValue[0] = '\0';
      }
      break;
    case IMDB_TYPE_EPOCH:
      result->epochValue = field->epochValue;
      break;
    case IMDB_TYPE_BOOL:
      result->boolValue = field->boolValue;
      break;
    case IMDB_TYPE_FLOAT:
      result->floatValue = field->floatValue;
      break;
  }
}

// Select a single column value from first matching record
IMDBResult ESP32IMDB::select(const char* column, const char* whereColumn,
                            const void* whereValue, IMDBSelectResult* result) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (column == nullptr || whereColumn == nullptr || whereValue == nullptr || result == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  int colIdx = findColumnIndex(column);
  int whereIdx = findColumnIndex(whereColumn);
  
  if (colIdx < 0 || whereIdx < 0) {
    unlock();
    return IMDB_ERROR_COLUMN_NOT_FOUND;
  }
  
  result->hasValue = false;
  
  for (int i = 0; i < _recordCount; i++) {
    if (!_records[i].isValid || isRecordExpired(_records[i].expiryMillis)) {
      continue;
    }
    
    if (compareValues(&_records[i].fields[whereIdx], whereValue, _columns[whereIdx].type)) {
      getFieldValue(&_records[i].fields[colIdx], _columns[colIdx].type, result);
      unlock();
      return IMDB_OK;
    }
  }
  
  unlock();
  return IMDB_ERROR_NO_RECORDS;
}

// Select all matching records (caller must free results)
IMDBResult ESP32IMDB::selectAll(const char* whereColumn, const void* whereValue,
                               IMDBSelectResult** results, int* resultCount) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (whereColumn == nullptr || whereValue == nullptr || results == nullptr || resultCount == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  int whereIdx = findColumnIndex(whereColumn);
  if (whereIdx < 0) {
    unlock();
    return IMDB_ERROR_COLUMN_NOT_FOUND;
  }
  
  // Count matches first
  int matches = 0;
  for (int i = 0; i < _recordCount; i++) {
    if (_records[i].isValid && !isRecordExpired(_records[i].expiryMillis) &&
        compareValues(&_records[i].fields[whereIdx], whereValue, _columns[whereIdx].type)) {
      matches++;
    }
  }
  
  if (matches == 0) {
    *results = nullptr;
    *resultCount = 0;
    unlock();
    return IMDB_ERROR_NO_RECORDS;
  }
  
  // Allocate result array
  *results = (IMDBSelectResult*)malloc(sizeof(IMDBSelectResult) * matches * _columnCount);
  if (*results == nullptr) {
    unlock();
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  // Fill results
  int resultIdx = 0;
  for (int i = 0; i < _recordCount; i++) {
    if (_records[i].isValid && !isRecordExpired(_records[i].expiryMillis) &&
        compareValues(&_records[i].fields[whereIdx], whereValue, _columns[whereIdx].type)) {
      for (int col = 0; col < _columnCount; col++) {
        getFieldValue(&_records[i].fields[col], _columns[col].type, 
                     &(*results)[resultIdx * _columnCount + col]);
      }
      resultIdx++;
    }
  }
  
  *resultCount = matches;
  unlock();
  return IMDB_OK;
}

// Count all valid records
int32_t ESP32IMDB::count() {
  lock();
  
  if (!_tableExists) {
    unlock();
    return 0;
  }
  
  int32_t cnt = 0;
  for (int i = 0; i < _recordCount; i++) {
    if (_records[i].isValid && !isRecordExpired(_records[i].expiryMillis)) {
      cnt++;
    }
  }
  
  unlock();
  return cnt;
}

// Count records matching WHERE condition
int32_t ESP32IMDB::countWhere(const char* whereColumn, const void* whereValue) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return 0;
  }
  
  if (whereColumn == nullptr || whereValue == nullptr) {
    unlock();
    return 0;
  }
  
  int whereIdx = findColumnIndex(whereColumn);
  if (whereIdx < 0) {
    unlock();
    return 0;
  }
  
  int32_t cnt = 0;
  for (int i = 0; i < _recordCount; i++) {
    if (_records[i].isValid && !isRecordExpired(_records[i].expiryMillis) &&
        compareValues(&_records[i].fields[whereIdx], whereValue, _columns[whereIdx].type)) {
      cnt++;
    }
  }
  
  unlock();
  return cnt;
}

// Find minimum value in a column
IMDBResult ESP32IMDB::min(const char* column, IMDBSelectResult* result) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (column == nullptr || result == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  int colIdx = findColumnIndex(column);
  if (colIdx < 0) {
    unlock();
    return IMDB_ERROR_COLUMN_NOT_FOUND;
  }
  
  // Only numeric types supported
  IMDBDataType type = _columns[colIdx].type;
  if (type != IMDB_TYPE_INT32 && type != IMDB_TYPE_EPOCH && type != IMDB_TYPE_FLOAT) {
    unlock();
    return IMDB_ERROR_INVALID_TYPE;
  }
  
  result->hasValue = false;
  int32_t minVal = 0x7FFFFFFF;  // Max int32
  float minFloat = 3.4028235e38f;  // Max float
  
  for (int i = 0; i < _recordCount; i++) {
    if (!_records[i].isValid || isRecordExpired(_records[i].expiryMillis)) {
      continue;
    }
    
    if (type == IMDB_TYPE_FLOAT) {
      float val = _records[i].fields[colIdx].floatValue;
      if (!result->hasValue || val < minFloat) {
        minFloat = val;
        result->hasValue = true;
      }
    } else {
      int32_t val = (type == IMDB_TYPE_INT32) 
                    ? _records[i].fields[colIdx].int32Value
                    : (int32_t)_records[i].fields[colIdx].epochValue;
      
      if (!result->hasValue || val < minVal) {
        minVal = val;
        result->hasValue = true;
      }
    }
  }
  
  if (!result->hasValue) {
    unlock();
    return IMDB_ERROR_NO_RECORDS;
  }
  
  result->type = type;
  if (type == IMDB_TYPE_INT32) {
    result->int32Value = minVal;
  } else if (type == IMDB_TYPE_EPOCH) {
    result->epochValue = (uint32_t)minVal;
  } else {
    result->floatValue = minFloat;
  }
  
  unlock();
  return IMDB_OK;
}

// Find maximum value in a column
IMDBResult ESP32IMDB::max(const char* column, IMDBSelectResult* result) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (column == nullptr || result == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  int colIdx = findColumnIndex(column);
  if (colIdx < 0) {
    unlock();
    return IMDB_ERROR_COLUMN_NOT_FOUND;
  }
  
  // Only numeric types supported
  IMDBDataType type = _columns[colIdx].type;
  if (type != IMDB_TYPE_INT32 && type != IMDB_TYPE_EPOCH && type != IMDB_TYPE_FLOAT) {
    unlock();
    return IMDB_ERROR_INVALID_TYPE;
  }
  
  result->hasValue = false;
  int32_t maxVal = (int32_t)0x80000000;  // Min int32
  float maxFloat = -3.4028235e38f;  // Min float
  
  for (int i = 0; i < _recordCount; i++) {
    if (!_records[i].isValid || isRecordExpired(_records[i].expiryMillis)) {
      continue;
    }
    
    if (type == IMDB_TYPE_FLOAT) {
      float val = _records[i].fields[colIdx].floatValue;
      if (!result->hasValue || val > maxFloat) {
        maxFloat = val;
        result->hasValue = true;
      }
    } else {
      int32_t val = (type == IMDB_TYPE_INT32)
                    ? _records[i].fields[colIdx].int32Value
                    : (int32_t)_records[i].fields[colIdx].epochValue;
      
      if (!result->hasValue || val > maxVal) {
        maxVal = val;
        result->hasValue = true;
      }
    }
  }
  
  if (!result->hasValue) {
    unlock();
    return IMDB_ERROR_NO_RECORDS;
  }
  
  result->type = type;
  if (type == IMDB_TYPE_INT32) {
    result->int32Value = maxVal;
  } else if (type == IMDB_TYPE_EPOCH) {
    result->epochValue = (uint32_t)maxVal;
  } else {
    result->floatValue = maxFloat;
  }
  
  unlock();
  return IMDB_OK;
}

// Get top N records (caller must free results)
IMDBResult ESP32IMDB::top(int n, IMDBSelectResult** results, int* resultCount) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (results == nullptr || resultCount == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  // Count valid records
  int validCount = 0;
  for (int i = 0; i < _recordCount; i++) {
    if (_records[i].isValid && !isRecordExpired(_records[i].expiryMillis)) {
      validCount++;
    }
  }
  
  if (validCount == 0) {
    *results = nullptr;
    *resultCount = 0;
    unlock();
    return IMDB_ERROR_NO_RECORDS;
  }
  
  int returnCount = (n < validCount) ? n : validCount;
  
  // Allocate result array
  *results = (IMDBSelectResult*)malloc(sizeof(IMDBSelectResult) * returnCount * _columnCount);
  if (*results == nullptr) {
    unlock();
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  // Fill results
  int resultIdx = 0;
  for (int i = 0; i < _recordCount && resultIdx < returnCount; i++) {
    if (_records[i].isValid && !isRecordExpired(_records[i].expiryMillis)) {
      for (int col = 0; col < _columnCount; col++) {
        getFieldValue(&_records[i].fields[col], _columns[col].type,
                     &(*results)[resultIdx * _columnCount + col]);
      }
      resultIdx++;
    }
  }
  
  *resultCount = returnCount;
  unlock();
  return IMDB_OK;
}

// Get total number of records (including invalid)
int ESP32IMDB::getRecordCount() const {
  lock();
  int count = _recordCount;
  unlock();
  return count;
}

// Estimate memory usage
size_t ESP32IMDB::getMemoryUsage() const {
  lock();
  
  size_t total = 0;
  
  // Column definitions
  total += sizeof(IMDBColumn) * _columnCount;
  
  // Record array
  total += sizeof(IMDBRecord) * _recordCapacity;
  
  // Fields in each record
  if (_columns != nullptr && _records != nullptr) {
    for (int i = 0; i < _recordCount; i++) {
      if (_records[i].fields != nullptr) {
        total += sizeof(IMDBFieldValue) * _columnCount;
        
        // String allocations
        for (int j = 0; j < _columnCount; j++) {
          if (_columns[j].type == IMDB_TYPE_STRING && _records[i].fields[j].stringValue != nullptr) {
            total += strlen(_records[i].fields[j].stringValue) + 1;
          }
        }
      }
    }
  }
  
  unlock();
  return total;
}

// Helper function to validate hexadecimal character
static bool isHexChar(char c) {
  return (c >= '0' && c <= '9') || 
         (c >= 'a' && c <= 'f') || 
         (c >= 'A' && c <= 'F');
}

// Parse MAC address from string (supports multiple formats)
bool ESP32IMDB::parseMacAddress(const char* macStr, uint8_t* macBytes) {
  if (macStr == nullptr || macBytes == nullptr) {
    return false;
  }
  
  int len = strlen(macStr);
  
  // Check for 12-character format (no delimiters): aabbccddeeff
  if (len == 12) {
    // Validate all characters are hex
    for (int i = 0; i < 12; i++) {
      if (!isHexChar(macStr[i])) {
        return false;
      }
    }
    
    // Parse hex pairs
    for (int i = 0; i < 6; i++) {
      char hex[3] = {macStr[i*2], macStr[i*2+1], '\0'};
      macBytes[i] = (uint8_t)strtol(hex, nullptr, 16);
    }
    return true;
  }
  
  // Check for 17-character format with delimiters: aa:bb:cc:dd:ee:ff or aa-bb-cc-dd-ee-ff
  if (len == 17) {
    char delimiter = macStr[2];
    if (delimiter != ':' && delimiter != '-') {
      return false;
    }
    
    for (int i = 0; i < 6; i++) {
      int pos = i * 3;
      
      // Validate hex characters for this octet
      if (!isHexChar(macStr[pos]) || !isHexChar(macStr[pos + 1])) {
        return false;
      }
      
      char hex[3] = {macStr[pos], macStr[pos + 1], '\0'};
      macBytes[i] = (uint8_t)strtol(hex, nullptr, 16);
      
      // Check delimiters
      if (i < 5 && macStr[pos + 2] != delimiter) {
        return false;
      }
    }
    return true;
  }
  
  return false;
}

// Format MAC address as colon-separated string (output is 18 bytes)
void ESP32IMDB::formatMacAddress(const uint8_t* macBytes, char* output) {
  if (macBytes == nullptr || output == nullptr) {
    if (output != nullptr) {
      output[0] = '\0';
    }
    return;
  }
  
  sprintf(output, "%02x:%02x:%02x:%02x:%02x:%02x",
          macBytes[0], macBytes[1], macBytes[2],
          macBytes[3], macBytes[4], macBytes[5]);
}

// Check if the database is thread-safe (mutex was created successfully)
bool ESP32IMDB::isThreadSafe() const {
  return _mutex != nullptr;
}

#if IMDB_ENABLE_PERSISTENCE

// Save database to SPIFFS file
IMDBResult ESP32IMDB::saveToFile(const char* filename) {
  lock();
  
  if (!_tableExists) {
    unlock();
    return IMDB_ERROR_NO_TABLE;
  }
  
  if (filename == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  // Purge expired records before saving (inline to avoid deadlock)
  for (int i = 0; i < _recordCount; i++) {
    if (_records[i].isValid && isRecordExpired(_records[i].expiryMillis)) {
      freeRecord(&_records[i]);
      _records[i].isValid = false;
    }
  }
  compactRecords();
  
  // Check record count limit for file format (uint16_t)
  if (_recordCount > 65535) {
    unlock();
    return IMDB_ERROR_INVALID_OPERATION;  // Too many records for file format
  }
  
  // Create temporary filename for atomic write
  char tempFilename[256];
  snprintf(tempFilename, sizeof(tempFilename), "%s.tmp", filename);
  
  // Open file for writing
  File file = SPIFFS.open(tempFilename, "w");
  if (!file) {
    unlock();
    return IMDB_ERROR_FILE_OPEN;
  }
  
  // Write header
  const char magic[4] = {'I', 'M', 'D', 'B'};
  const uint8_t version = 1;
  uint16_t recordCount = (uint16_t)_recordCount;
  uint32_t saveMillis = millis();
  
  if (file.write((const uint8_t*)magic, 4) != 4) {
    file.close();
    SPIFFS.remove(tempFilename);
    unlock();
    return IMDB_ERROR_FILE_WRITE;
  }
  
  if (file.write(&version, 1) != 1) {
    file.close();
    SPIFFS.remove(tempFilename);
    unlock();
    return IMDB_ERROR_FILE_WRITE;
  }
  
  if (file.write(&_columnCount, 1) != 1) {
    file.close();
    SPIFFS.remove(tempFilename);
    unlock();
    return IMDB_ERROR_FILE_WRITE;
  }
  
  if (file.write((const uint8_t*)&recordCount, 2) != 2) {
    file.close();
    SPIFFS.remove(tempFilename);
    unlock();
    return IMDB_ERROR_FILE_WRITE;
  }
  
  if (file.write((const uint8_t*)&saveMillis, 4) != 4) {
    file.close();
    SPIFFS.remove(tempFilename);
    unlock();
    return IMDB_ERROR_FILE_WRITE;
  }
  
  // Write schema
  for (int i = 0; i < _columnCount; i++) {
    if (file.write((const uint8_t*)_columns[i].name, 32) != 32) {
      file.close();
      SPIFFS.remove(tempFilename);
      unlock();
      return IMDB_ERROR_FILE_WRITE;
    }
    
    uint8_t typeValue = (uint8_t)_columns[i].type;
    if (file.write(&typeValue, 1) != 1) {
      file.close();
      SPIFFS.remove(tempFilename);
      unlock();
      return IMDB_ERROR_FILE_WRITE;
    }
  }
  
  // Write records
  for (int i = 0; i < _recordCount; i++) {
    IMDBRecord* record = &_records[i];
    
    // Write isValid flag
    uint8_t isValid = record->isValid ? 1 : 0;
    if (file.write(&isValid, 1) != 1) {
      file.close();
      SPIFFS.remove(tempFilename);
      unlock();
      return IMDB_ERROR_FILE_WRITE;
    }
    
    // Write expiryMillis
    if (file.write((const uint8_t*)&record->expiryMillis, 4) != 4) {
      file.close();
      SPIFFS.remove(tempFilename);
      unlock();
      return IMDB_ERROR_FILE_WRITE;
    }
    
    // Write fields
    for (int j = 0; j < _columnCount; j++) {
      IMDBFieldValue* field = &record->fields[j];
      IMDBDataType type = _columns[j].type;
      
      switch (type) {
        case IMDB_TYPE_INT32:
          if (file.write((const uint8_t*)&field->int32Value, 4) != 4) {
            file.close();
            SPIFFS.remove(tempFilename);
            unlock();
            return IMDB_ERROR_FILE_WRITE;
          }
          break;
          
        case IMDB_TYPE_FLOAT:
          if (file.write((const uint8_t*)&field->floatValue, 4) != 4) {
            file.close();
            SPIFFS.remove(tempFilename);
            unlock();
            return IMDB_ERROR_FILE_WRITE;
          }
          break;
          
        case IMDB_TYPE_BOOL: {
          uint8_t boolValue = field->boolValue ? 1 : 0;
          if (file.write(&boolValue, 1) != 1) {
            file.close();
            SPIFFS.remove(tempFilename);
            unlock();
            return IMDB_ERROR_FILE_WRITE;
          }
          break;
        }
        
        case IMDB_TYPE_MAC:
          if (file.write(field->macAddress, 6) != 6) {
            file.close();
            SPIFFS.remove(tempFilename);
            unlock();
            return IMDB_ERROR_FILE_WRITE;
          }
          break;
          
        case IMDB_TYPE_EPOCH:
          if (file.write((const uint8_t*)&field->epochValue, 4) != 4) {
            file.close();
            SPIFFS.remove(tempFilename);
            unlock();
            return IMDB_ERROR_FILE_WRITE;
          }
          break;
          
        case IMDB_TYPE_STRING: {
          uint8_t length = 0;
          if (field->stringValue) {
            size_t strLen = strlen(field->stringValue);
            // Clamp to max string length for safety
            length = (strLen > IMDB_MAX_STRING_LENGTH) ? IMDB_MAX_STRING_LENGTH : (uint8_t)strLen;
          }
          if (file.write(&length, 1) != 1) {
            file.close();
            SPIFFS.remove(tempFilename);
            unlock();
            return IMDB_ERROR_FILE_WRITE;
          }
          
          if (length > 0) {
            if (file.write((const uint8_t*)field->stringValue, length) != length) {
              file.close();
              SPIFFS.remove(tempFilename);
              unlock();
              return IMDB_ERROR_FILE_WRITE;
            }
          }
          break;
        }
      }
    }
  }
  
  file.close();
  
  // Atomic rename - replace old file with new one
  if (SPIFFS.exists(filename)) {
    SPIFFS.remove(filename);
  }
  if (!SPIFFS.rename(tempFilename, filename)) {
    SPIFFS.remove(tempFilename);
    unlock();
    return IMDB_ERROR_FILE_WRITE;
  }
  
  unlock();
  return IMDB_OK;
}

// Load database from SPIFFS file
IMDBResult ESP32IMDB::loadFromFile(const char* filename) {
  lock();
  
  if (filename == nullptr) {
    unlock();
    return IMDB_ERROR_INVALID_VALUE;
  }
  
  // Check if table already exists - don't overwrite
  if (_tableExists) {
    unlock();
    return IMDB_ERROR_TABLE_EXISTS;
  }
  
  // Check if file exists first
  if (!SPIFFS.exists(filename)) {
    unlock();
    return IMDB_ERROR_FILE_OPEN;
  }
  
  // Open file for reading
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    unlock();
    return IMDB_ERROR_FILE_OPEN;
  }
  
  // Read and validate header
  char magic[4];
  if (file.read((uint8_t*)magic, 4) != 4 || 
      magic[0] != 'I' || magic[1] != 'M' || magic[2] != 'D' || magic[3] != 'B') {
    file.close();
    unlock();
    return IMDB_ERROR_CORRUPT_FILE;
  }
  
  uint8_t version;
  if (file.read(&version, 1) != 1 || version != 1) {
    file.close();
    unlock();
    return IMDB_ERROR_CORRUPT_FILE;
  }
  
  uint8_t columnCount;
  if (file.read(&columnCount, 1) != 1) {
    file.close();
    unlock();
    return IMDB_ERROR_CORRUPT_FILE;
  }
  
  uint16_t recordCount;
  if (file.read((uint8_t*)&recordCount, 2) != 2) {
    file.close();
    unlock();
    return IMDB_ERROR_CORRUPT_FILE;
  }
  
  uint32_t saveMillis;
  if (file.read((uint8_t*)&saveMillis, 4) != 4) {
    file.close();
    unlock();
    return IMDB_ERROR_CORRUPT_FILE;
  }
  
  // Read schema
  if (!checkHeapLimit()) {
    file.close();
    unlock();
    return IMDB_ERROR_HEAP_LIMIT;
  }
  
  _columns = (IMDBColumn*)malloc(sizeof(IMDBColumn) * columnCount);
  if (_columns == nullptr) {
    file.close();
    unlock();
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  for (int i = 0; i < columnCount; i++) {
    if (file.read((uint8_t*)_columns[i].name, 32) != 32) {
      free(_columns);
      _columns = nullptr;
      file.close();
      unlock();
      return IMDB_ERROR_FILE_READ;
    }
    
    uint8_t typeValue;
    if (file.read(&typeValue, 1) != 1) {
      free(_columns);
      _columns = nullptr;
      file.close();
      unlock();
      return IMDB_ERROR_FILE_READ;
    }
    _columns[i].type = (IMDBDataType)typeValue;
  }
  
  _columnCount = columnCount;
  _tableExists = true;
  
  // Allocate records array
  if (!checkHeapLimit()) {
    free(_columns);
    _columns = nullptr;
    _tableExists = false;
    file.close();
    unlock();
    return IMDB_ERROR_HEAP_LIMIT;
  }
  
  _recordCapacity = recordCount > 10 ? recordCount : 10;
  _records = (IMDBRecord*)malloc(sizeof(IMDBRecord) * _recordCapacity);
  if (_records == nullptr) {
    free(_columns);
    _columns = nullptr;
    _tableExists = false;
    file.close();
    unlock();
    return IMDB_ERROR_OUT_OF_MEMORY;
  }
  
  _recordCount = 0;
  uint32_t currentMillis = millis();
  
  // Read records
  for (int i = 0; i < recordCount; i++) {
    IMDBRecord record;
    
    // Read isValid flag
    uint8_t isValid;
    if (file.read(&isValid, 1) != 1) {
      // Cleanup on error
      for (int j = 0; j < _recordCount; j++) {
        freeRecord(&_records[j]);
      }
      free(_records);
      free(_columns);
      _records = nullptr;
      _columns = nullptr;
      _recordCount = 0;
      _recordCapacity = 0;
      _tableExists = false;
      file.close();
      unlock();
      return IMDB_ERROR_FILE_READ;
    }
    record.isValid = (isValid != 0);
    
    // Read expiryMillis
    uint32_t savedExpiryMillis;
    if (file.read((uint8_t*)&savedExpiryMillis, 4) != 4) {
      for (int j = 0; j < _recordCount; j++) {
        freeRecord(&_records[j]);
      }
      free(_records);
      free(_columns);
      _records = nullptr;
      _columns = nullptr;
      _recordCount = 0;
      _recordCapacity = 0;
      _tableExists = false;
      file.close();
      unlock();
      return IMDB_ERROR_FILE_READ;
    }
    
    // Adjust TTL based on time difference
    if (savedExpiryMillis == 0) {
      record.expiryMillis = 0;  // No expiry
    } else {
      uint32_t remainingMillis = savedExpiryMillis - saveMillis;
      record.expiryMillis = currentMillis + remainingMillis;
    }
    
    // Allocate fields array
    if (!checkHeapLimit()) {
      for (int j = 0; j < _recordCount; j++) {
        freeRecord(&_records[j]);
      }
      free(_records);
      free(_columns);
      _records = nullptr;
      _columns = nullptr;
      _recordCount = 0;
      _recordCapacity = 0;
      _tableExists = false;
      file.close();
      unlock();
      return IMDB_ERROR_HEAP_LIMIT;
    }
    
    record.fields = (IMDBFieldValue*)malloc(sizeof(IMDBFieldValue) * _columnCount);
    if (record.fields == nullptr) {
      for (int j = 0; j < _recordCount; j++) {
        freeRecord(&_records[j]);
      }
      free(_records);
      free(_columns);
      _records = nullptr;
      _columns = nullptr;
      _recordCount = 0;
      _recordCapacity = 0;
      _tableExists = false;
      file.close();
      unlock();
      return IMDB_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize string pointers to nullptr
    for (int j = 0; j < _columnCount; j++) {
      if (_columns[j].type == IMDB_TYPE_STRING) {
        record.fields[j].stringValue = nullptr;
      }
    }
    
    // Read fields
    bool readError = false;
    for (int j = 0; j < _columnCount; j++) {
      IMDBFieldValue* field = &record.fields[j];
      IMDBDataType type = _columns[j].type;
      
      switch (type) {
        case IMDB_TYPE_INT32:
          if (file.read((uint8_t*)&field->int32Value, 4) != 4) {
            readError = true;
          }
          break;
          
        case IMDB_TYPE_FLOAT:
          if (file.read((uint8_t*)&field->floatValue, 4) != 4) {
            readError = true;
          }
          break;
          
        case IMDB_TYPE_BOOL: {
          uint8_t boolValue;
          if (file.read(&boolValue, 1) != 1) {
            readError = true;
          } else {
            field->boolValue = (boolValue != 0);
          }
          break;
        }
        
        case IMDB_TYPE_MAC:
          if (file.read(field->macAddress, 6) != 6) {
            readError = true;
          }
          break;
          
        case IMDB_TYPE_EPOCH:
          if (file.read((uint8_t*)&field->epochValue, 4) != 4) {
            readError = true;
          }
          break;
          
        case IMDB_TYPE_STRING: {
          uint8_t length;
          if (file.read(&length, 1) != 1) {
            readError = true;
            break;
          }
          
          // Validate string length
          if (length > IMDB_MAX_STRING_LENGTH) {
            readError = true;
            break;
          }
          
          if (length > 0) {
            field->stringValue = (char*)malloc(length + 1);
            if (field->stringValue == nullptr) {
              readError = true;
              break;
            }
            
            if (file.read((uint8_t*)field->stringValue, length) != length) {
              free(field->stringValue);
              field->stringValue = nullptr;
              readError = true;
              break;
            }
            field->stringValue[length] = '\0';
          } else {
            field->stringValue = nullptr;
          }
          break;
        }
      }
      
      if (readError) {
        break;
      }
    }
    
    if (readError) {
      // Free this record's fields
      for (int j = 0; j < _columnCount; j++) {
        if (_columns[j].type == IMDB_TYPE_STRING && record.fields[j].stringValue != nullptr) {
          free(record.fields[j].stringValue);
        }
      }
      free(record.fields);
      
      // Cleanup all previous records
      for (int j = 0; j < _recordCount; j++) {
        freeRecord(&_records[j]);
      }
      free(_records);
      free(_columns);
      _records = nullptr;
      _columns = nullptr;
      _recordCount = 0;
      _recordCapacity = 0;
      _tableExists = false;
      file.close();
      unlock();
      return IMDB_ERROR_FILE_READ;
    }
    
    // Add record to array
    _records[_recordCount] = record;
    _recordCount++;
  }
  
  file.close();
  unlock();
  return IMDB_OK;
}

#endif // IMDB_ENABLE_PERSISTENCE

// Convert result code to string
const char* ESP32IMDB::resultToString(IMDBResult result) {
  switch (result) {
    case IMDB_OK: return "OK";
    case IMDB_ERROR_OUT_OF_MEMORY: return "Out of memory";
    case IMDB_ERROR_HEAP_LIMIT: return "Heap limit exceeded";
    case IMDB_ERROR_TABLE_EXISTS: return "Table already exists";
    case IMDB_ERROR_NO_TABLE: return "No table exists";
    case IMDB_ERROR_INVALID_TYPE: return "Invalid data type";
    case IMDB_ERROR_INVALID_VALUE: return "Invalid value";
    case IMDB_ERROR_COLUMN_COUNT_MISMATCH: return "Column count mismatch";
    case IMDB_ERROR_COLUMN_NOT_FOUND: return "Column not found";
    case IMDB_ERROR_INVALID_OPERATION: return "Invalid operation";
    case IMDB_ERROR_NO_RECORDS: return "No records found";
    case IMDB_ERROR_INVALID_MAC_FORMAT: return "Invalid MAC address format";
#if IMDB_ENABLE_PERSISTENCE
    case IMDB_ERROR_FILE_OPEN: return "Failed to open file";
    case IMDB_ERROR_FILE_WRITE: return "Failed to write to file";
    case IMDB_ERROR_FILE_READ: return "Failed to read from file";
    case IMDB_ERROR_CORRUPT_FILE: return "Corrupt or invalid file format";
#endif
    default: return "Unknown error";
  }
}
