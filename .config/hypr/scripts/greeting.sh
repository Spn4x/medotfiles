#!/bin/bash

hour=$(date +"%H")

if (( hour >= 5 && hour < 12 )); then
  echo "Good morning, $USER"
elif (( hour >= 12 && hour < 18 )); then
  echo "Good afternoon, $USER"
else
  echo "Good evening, $USER"
fi