#!/usr/bin/env bash

IMAGE_NAME="cpp-env"
DOCKERFILE="docker/Dockerfile"

# Exit on error
set -e

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed."
    exit 1
fi

# Check if image exists and rebuild
if ! docker image inspect "$IMAGE_NAME" > /dev/null 2>&1; then
    echo "🟢 Docker image does not exist. Will build."
    docker build -t "$IMAGE_NAME" -f "$DOCKERFILE" .
    echo "✅ Docker image built successfully."
fi

# Run container
echo "🟢 Starting Docker container."
docker run --rm -it \
    -v "$PWD:/project" \
    "$IMAGE_NAME" \
    bash

