#!/usr/bin/env bash

IMAGE_NAME="cpp-env"

# Exit on error
set -e

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed."
    exit 1
fi

# Stop running containers from this image
RUNNING=$(docker ps -q --filter "ancestor=$IMAGE_NAME")
if [ -n "$RUNNING" ]; then
    echo "🟢 Stopping running containers."
    docker stop $RUNNING
    echo "✅ Containers stopped."
else
    echo "🟢 No running containers found."
fi

# Remove any remaining containers from this image
REMAINING=$(docker ps -aq --filter "ancestor=$IMAGE_NAME")
if [ -n "$REMAINING" ]; then
    echo "🟢 Removing containers."
    docker rm -f $REMAINING
    echo "✅ Containers removed."
fi

# Remove image
if docker image inspect "$IMAGE_NAME" > /dev/null 2>&1; then
    echo "🟢 Removing Docker image."
    docker rmi "$IMAGE_NAME"
    echo "✅ Docker image removed successfully."
else
    echo "🟢 Docker image does not exist. Nothing to remove."
fi
