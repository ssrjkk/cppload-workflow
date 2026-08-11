#!/bin/bash
# Setup script for cppload-pro demo environment

set -e

echo "Setting up cppload-pro demo environment..."

# Check Docker
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed"
    exit 1
fi

if ! command -v docker-compose &> /dev/null; then
    echo "Error: docker-compose is not installed"
    exit 1
fi

# Create necessary directories
mkdir -p kong
mkdir -p prometheus_data
mkdir -p grafana_data

# kong.yml is committed next to this script and mounted by docker-compose.yml

# Start the environment
echo "Starting demo environment..."
docker-compose up -d

# Wait for services to be ready
echo "Waiting for services to start..."
sleep 10

# Check services
echo "Checking services..."
docker-compose ps

echo ""
echo "Demo environment is ready!"
echo ""
echo "Access points:"
echo "  Gateway:    http://localhost:8080"
echo "  Grafana:    http://localhost:3000 (admin/admin)"
echo "  Prometheus:  http://localhost:9090"
echo "  Jaeger:     http://localhost:16686"
echo ""
echo "To run a load test:"
echo "  cppload-cli --target http://localhost:8080 --rps 1000 --duration 60"
echo ""
echo "To stop the environment:"
echo "  docker-compose down -v"
