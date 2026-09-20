#!/bin/sh

TZ_LIB=/usr/bin/rockbase_timezone.sh
[ -r "$TZ_LIB" ] || TZ_LIB=""

url_decode() {
  printf '%b' "$(printf '%s' "$1" | sed 's/+/ /g; s/%/\\x/g')"
}

get_params() {
  params="$QUERY_STRING"
  if [ "$REQUEST_METHOD" = "POST" ]; then
    body=$(cat)
    if [ -n "$params" ] && [ -n "$body" ]; then
      params="${params}&${body}"
    elif [ -n "$body" ]; then
      params="$body"
    fi
  fi
}

get_param() {
  key=$1
  old_ifs=$IFS
  IFS='&'
  set -- $params
  IFS=$old_ifs
  for pair in "$@"; do
    case "$pair" in
      *=*)
        name=${pair%%=*}
        value=${pair#*=}
        if [ "$name" = "$key" ]; then
          url_decode "$value"
          return 0
        fi
        ;;
    esac
  done
  return 1
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\r//g'
}

json_str() {
  printf '"%s"' "$(json_escape "$1")"
}

tz_get_endpoint() {
  current=""
  if [ -n "$TZ_LIB" ]; then
    current=$("$TZ_LIB" status 2>/dev/null | head -n1)
  fi
  [ -n "$current" ] || current="UTC"
  printf '{'
  printf '"ok":true,'
  printf '"timezone":%s' "$(json_str "$current")"
  printf '}'
}

tz_apply_endpoint() {
  get_params
  zone=$(get_param timezone 2>/dev/null || true)
  [ -n "$zone" ] || zone=$(get_param tz 2>/dev/null || true)

  # Validate: must be a non-empty IANA-style path (Dir/Name) or "UTC".
  # Reject anything with shell metacharacters to avoid injection into the
  # apply script's ln/cp/pipeline.
  case "$zone" in
    ""|*[!A-Za-z0-9/_-]*)
      printf '{"ok":false,"error":"invalid timezone"}'
      return 1
      ;;
  esac
  # Only allow a single slash (Dir/Name) or none (UTC).
  slash_count=$(printf '%s' "$zone" | awk -F'/' '{print NF-1}')
  if [ "$slash_count" -gt 1 ]; then
    printf '{"ok":false,"error":"invalid timezone"}'
    return 1
  fi

  if [ -z "$TZ_LIB" ]; then
    printf '{"ok":false,"error":"timezone helper not found"}'
    return 1
  fi

  # apply_tz writes /etc/localtime + /etc/TZ and exports TZ in its own shell;
  # the running daemons pick it up on their next clock read.
  if "$TZ_LIB" apply "$zone" >/dev/null 2>&1; then
    new_tz=$("$TZ_LIB" status 2>/dev/null | head -n1)
    [ -n "$new_tz" ] || new_tz="$zone"
    printf '{'
    printf '"ok":true,'
    printf '"timezone":%s' "$(json_str "$new_tz")"
    printf '}'
  else
    printf '{"ok":false,"error":"failed to apply timezone"}'
    return 1
  fi
}

echo "Content-Type: application/json; charset=utf-8"
echo "Cache-Control: no-store, no-cache, must-revalidate"
echo "Pragma: no-cache"
echo ""

get_params
action=$(get_param action 2>/dev/null || true)
[ -n "$action" ] || action=get

case "$action" in
  get|status) tz_get_endpoint ;;
  apply) tz_apply_endpoint ;;
  *)
    printf '{"ok":false,"error":"unknown action"}'
    ;;
esac
