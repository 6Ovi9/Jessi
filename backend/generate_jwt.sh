#!/bin/bash
# Script to generate Supabase JWT keys based on the JWT_SECRET in .env

# Move to the backend directory
cd "$(dirname "$0")"

if [ ! -f .env ]; then
  echo "Error: .env file not found!"
  exit 1
fi

# Extract JWT_SECRET
JWT_SECRET=$(grep '^JWT_SECRET=' .env | cut -d '=' -f2- | tr -d '\r')

if [ -z "$JWT_SECRET" ]; then
  echo "Error: JWT_SECRET is empty in .env"
  exit 1
fi

if [ ${#JWT_SECRET} -lt 32 ]; then
  echo "Warning: JWT_SECRET is less than 32 characters long. Supabase requires a strong secret."
fi

# Function to generate JWT
generate_jwt() {
  local ROLE=$1
  local SECRET=$2
  
  local HEADER=$(echo -n '{"alg":"HS256","typ":"JWT"}' | base64 | tr -d '\n' | tr -d '=' | tr '/+' '_-')
  local PAYLOAD=$(echo -n '{"role":"'$ROLE'","iss":"supabase","iat":1700000000,"exp":2000000000}' | base64 | tr -d '\n' | tr -d '=' | tr '/+' '_-')
  local SIGNATURE=$(echo -n "$HEADER.$PAYLOAD" | openssl dgst -sha256 -mac HMAC -macopt hexkey:$(echo -n "$SECRET" | xxd -p -c 256 | tr -d '\n') -binary | base64 | tr -d '\n' | tr -d '=' | tr '/+' '_-')
  
  echo "$HEADER.$PAYLOAD.$SIGNATURE"
}

echo "Generating new JWT keys based on current JWT_SECRET..."

ANON_KEY=$(generate_jwt "anon" "$JWT_SECRET")
SERVICE_KEY=$(generate_jwt "service_role" "$JWT_SECRET")

# Update .env
if [[ "$OSTYPE" == "darwin"* ]]; then
  sed -i '' "s/^ANON_KEY=.*/ANON_KEY=$ANON_KEY/" .env
  sed -i '' "s/^SERVICE_KEY=.*/SERVICE_KEY=$SERVICE_KEY/" .env
else
  sed -i "s/^ANON_KEY=.*/ANON_KEY=$ANON_KEY/" .env
  sed -i "s/^SERVICE_KEY=.*/SERVICE_KEY=$SERVICE_KEY/" .env
fi

echo "Success! ANON_KEY and SERVICE_KEY updated in .env"
