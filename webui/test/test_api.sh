#!/bin/bash
# Test script for WSPR-ease REST API

BASE_URL="http://localhost:8080"
PASS=0
FAIL=0

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

test_endpoint() {
  local method=$1
  local endpoint=$2
  local expected_status=$3
  local description=$4
  local data=$5

  echo -n "Testing: $description ... "

  if [ "$method" == "GET" ]; then
    response=$(curl -s -w "\n%{http_code}" "$BASE_URL$endpoint")
  elif [ "$method" == "POST" ]; then
    response=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/json" -d "$data" "$BASE_URL$endpoint")
  elif [ "$method" == "PUT" ]; then
    response=$(curl -s -w "\n%{http_code}" -X PUT -H "Content-Type: application/json" -d "$data" "$BASE_URL$endpoint")
  elif [ "$method" == "DELETE" ]; then
    response=$(curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL$endpoint")
  fi

  http_code=$(echo "$response" | tail -n1)
  body=$(echo "$response" | head -n-1)

  if [ "$http_code" == "$expected_status" ]; then
    echo -e "${GREEN}PASS${NC} (HTTP $http_code)"
    ((PASS++))
  else
    echo -e "${RED}FAIL${NC} (Expected HTTP $expected_status, got $http_code)"
    echo "Response: $body"
    ((FAIL++))
  fi
}

echo "======================================"
echo "WSPR-ease REST API Test Suite"
echo "======================================"
echo ""

# Check if server is running
if ! curl -s "$BASE_URL/api/status" > /dev/null; then
  echo -e "${RED}ERROR: Server is not running at $BASE_URL${NC}"
  echo "Start the server with: make run"
  exit 1
fi

echo "Server is running. Starting tests..."
echo ""

# Configuration tests
test_endpoint "GET" "/api/config" "200" "Get configuration"
test_endpoint "GET" "/api/config/export" "200" "Export configuration"
test_endpoint "POST" "/api/config/reset" "200" "Reset configuration"

# Status test
test_endpoint "GET" "/api/status" "200" "Get system status"

# File operations tests
test_endpoint "GET" "/api/files?path=/" "200" "List root directory"
test_endpoint "PUT" "/api/files/test.txt" "200" "Upload test file" "Hello WSPR"
test_endpoint "GET" "/api/files/test.txt" "200" "Download test file"
test_endpoint "DELETE" "/api/files/test.txt" "200" "Delete test file"

# Transmission control
test_endpoint "POST" "/api/tx/trigger" "200" "Trigger transmission"

echo ""
echo "======================================"
echo "Test Results:"
echo -e "  ${GREEN}PASSED: $PASS${NC}"
echo -e "  ${RED}FAILED: $FAIL${NC}"
echo "======================================"

if [ $FAIL -eq 0 ]; then
  exit 0
else
  exit 1
fi
