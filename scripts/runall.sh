#!/usr/bin/env bash
#
# run v6.4 (The Final Version + CMake & Guard): The Definitive Universal Runner
#

# --- A) Prevent infinite recursion ---
[[ "$RUN_SELF_CALL" == "1" ]] && exit 0
export RUN_SELF_CALL=1

# --- 1. Setup & Defaults ---
SCRIPT_VERSION="v6.4 (The Final Version + CMake)"
INFO="\033[1;34m"; OK="\033[1;32m"; WARN="\033[1;33m"; ERR="\033[1;31m"; CMD_COLOR="\033[0;36m"; RESET="\033[0m"
DEFAULT_FILE_ORDER=(main.py app.py index.ts main.ts index.js main.go main.c main.cpp main.rs Makefile CMakeLists.txt)
TS_NODE_FLAGS="--transpile-only"
DEFAULT_FILE=""
PLUGIN_DIR="$HOME/.config/run/plugins"
ENABLE_LOGGING=false
LOG_FILE="$HOME/.cache/run/history.log"
LOG_MAX_LINES=1000
RUNRC_FILE=".runrc"

[ -f ~/.runrc.global ] && source ~/.runrc.global
[ -n "$DEFAULT_FILE" ] && DEFAULT_FILE_ORDER=("$DEFAULT_FILE" "${DEFAULT_FILE_ORDER[@]}")

# --- 2. Handler Registries ---
declare -A FILE_HANDLERS    # ext -> func
declare -A PROJECT_HANDLERS # marker -> func
declare -A INIT_HANDLERS    # lang -> func
declare -A REPL_HANDLERS    # lang -> func

# --- 3. Core Functions ---
show_help() {
  # Print the header with colors
  echo -e "\n${INFO}run ${SCRIPT_VERSION}${RESET}\n"
  cat <<EOF
Usage: run [flags] [target]

GENERATIVE & CLEANUP:
  --init <lang>       Create boilerplate (py, js, cpp, rs, go, lua, php, rb, pl, md, html)
  --clean             Remove binaries, .class, target/, dist/

CORE:
  run <file>          Run a single file
  run                 Auto-detect & run project or default file
  --repl <lang>       Launch REPL (py, node, rb, lua, bash, etc.)

INFO:
  -v, --version, --help, --list, --langs, --check, --detect

EXECUTION:
  --force             Rebuild even if up to date
  --debug             Echo commands before executing
  --include <path>    Add JARs to Java classpath
  --log               Enable command logging

EXTENSIBILITY:
  .runrc / ~/.runrc.global / ~/.config/run/plugins/

EOF
  exit 0
}

list_languages() {
  echo -e "${INFO}Supported Projects & File Types:${RESET}"
  echo "  Projects by marker: .runrc, setup.py, pyproject.toml, manage.py, app.py,"
  echo "                     pom.xml, build.gradle(.kts), Cargo.toml, package.json,"
  echo "                     go.mod, CMakeLists.txt, Makefile"
  echo "  File exts: (shebang), .jsx/.tsx, .ts, .py, .js, .go, .sh, .lua,"
  echo "             .php, .rb, .pl, .c/.cpp, .rs, .java, .html, .md"
  if [ -d "$PLUGIN_DIR" ] && [ "$(ls -A "$PLUGIN_DIR")" ]; then
    echo -e "\nPlugin-provided support:"
    for p in "$PLUGIN_DIR"/*.sh; do
      source "$p" &>/dev/null
      type plugin_help &>/dev/null && plugin_help
    done
  fi
  exit 0
}

check_environment() {
  echo -e "${INFO}Checking for required tools${RESET}"
  local tools=(python3 node gcc g++ rustc go java javac mvn cargo npm yarn pnpm poetry flask npx ts-node lua php ruby perl glow less make cmake)
  local ok=true
  for t in "${tools[@]}"; do
    if command -v "$t" &>/dev/null; then
      echo -e "  [${OK}✔${RESET}] $t"
    else
      echo -e "  [${ERR}✘${RESET}] $t"
      ok=false
    fi
  done
  $ok && echo -e "${OK}All essential tools found.${RESET}" || echo -e "${WARN}Some tools are missing.${RESET}"
  exit 0
}

list_detections() {
  echo -e "${INFO}Detections in this directory:${RESET}"
  local projects=() files=()
  # plugin markers
  for marker in "${!PROJECT_HANDLERS[@]}"; do
    [ -f "$marker" ] && projects+=("Plugin($marker)")
  done
  # built-ins
  for m in .runrc setup.py pyproject.toml manage.py app.py pom.xml build.gradle build.gradle.kts Cargo.toml package.json go.mod CMakeLists.txt Makefile; do
    [ -f "$m" ] && projects+=("$m")
  done
  for f in "${DEFAULT_FILE_ORDER[@]}"; do
    [ -f "$f" ] && files+=("$f")
  done

  if [ "${#projects[@]}" -eq 0 ]; then
    echo "  (none)"
  else
    printf '  - %s\n' "${projects[@]}"
  fi

  echo
  if [ "${#files[@]}" -eq 0 ]; then
    echo "No default runnable files."
  else
    printf '  - %s\n' "${files[@]}"
  fi
  exit 0
}

clean_artifacts() {
  echo -e "${INFO}Cleaning binaries, .class, target/, dist/${RESET}"
  local removed=false
  for f in *; do
    if [ -f "$f" ] && [ -x "$f" ] && [[ "$f" != *.* ]]; then
      rm -f "$f" && echo " Removed $f"; removed=true
    fi
  done
  if compgen -G "*.class" &>/dev/null; then
    rm -f *.class && echo " Removed Java .class files"; removed=true
  fi
  [ -d target ] && rm -rf target && echo " Removed Rust target/"; removed=true
  [ -d dist   ] && rm -rf dist   && echo " Removed JS dist/";   removed=true
  [ -d .run_tmp ] && rm -rf .run_tmp && echo " Removed .run_tmp/"; removed=true

  $removed || echo "No artifacts to clean."
  exit 0
}

prompt_add_to_gitignore() {
  local file_to_ignore="$1"
  # Check if we're in a git repo and the file exists
  if [ -d ".git" ] && [ -f ".gitignore" ]; then
      # Check if the file is NOT already in .gitignore
      if ! grep -qxF "$file_to_ignore" .gitignore; then
          read -p "Add $file_to_ignore to .gitignore? (y/N) " -n 1 -r
          echo # Move to a new line after the user's input
          if [[ $REPLY =~ ^[Yy]$ ]]; then
              echo "" >> .gitignore # ensure there's a newline before our entry
              echo "$file_to_ignore" >> .gitignore
              echo -e "${OK}Added $file_to_ignore to .gitignore.${RESET}"
          fi
      fi
  fi
}


# --- 4. Generative & REPL Handlers ---
INIT_HANDLERS[py]='echo "print(\"Hello, Python!\")" > main.py; echo Created main.py'
INIT_HANDLERS[js]='echo "console.log(\"Hello, JavaScript!\");" > index.js; echo Created index.js'
INIT_HANDLERS[cpp]=$'cat > main.cpp <<EOF\n#include <iostream>\nint main(){ std::cout<<"Hello, C++!\\n"; }\nEOF\necho Created main.cpp'
INIT_HANDLERS[c]=$'cat > main.c <<EOF\n#include <stdio.h>\nint main(){ printf("Hello, C!\\n"); }\nEOF\necho Created main.c'
INIT_HANDLERS[rs]='cargo new . --name my_rust_app && echo Created Rust project'
INIT_HANDLERS[go]='echo -e "package main\nimport \\"fmt\\"\nfunc main(){fmt.Println(\\"Hello, Go!\\")}" > main.go; echo Created main.go'
INIT_HANDLERS[ts]='echo "console.log(\"Hello, TypeScript!\");" > index.ts; echo Created index.ts'
INIT_HANDLERS[lua]='echo "print(\"Hello, Lua!\")" > main.lua; echo Created main.lua'
INIT_HANDLERS[php]='echo "<?php echo \"Hello, PHP!\\n\";?>" > index.php; echo Created index.php'
INIT_HANDLERS[rb]='echo "puts \"Hello, Ruby!\"" > main.rb; echo Created main.rb'
INIT_HANDLERS[pl]='echo "print \"Hello, Perl!\\n\";" > main.pl; echo Created main.pl'
INIT_HANDLERS[md]='echo "# Hello, Markdown!" > README.md; echo Created README.md'
INIT_HANDLERS[html]='echo "<!DOCTYPE html><html><body><h1>Hello, HTML!</h1></body></html>" > index.html; echo Created index.html'

REPL_HANDLERS[py]='exec python3'
REPL_HANDLERS[js]='exec node'
REPL_HANDLERS[node]='exec node'
REPL_HANDLERS[rb]='exec irb'
REPL_HANDLERS[lua]='exec lua'
REPL_HANDLERS[sh]='exec bash'
REPL_HANDLERS[bash]='exec bash'
REPL_HANDLERS[php]='exec php -a'

# --- 5. Execution Helper ---
execute_command() {
  local cmd="$1"

  # If .runrc doesn't exist, this is potentially the first run.
  # We'll save the command to be written out later if it succeeds.
  local cmd_to_save=""
  if [ ! -f "$RUNRC_FILE" ]; then
      cmd_to_save="$cmd"
  fi

  if $ENABLE_LOGGING; then
    mkdir -p "$(dirname "$LOG_FILE")"
    if [ -f "$LOG_FILE" ] && [ "$(wc -l < "$LOG_FILE")" -gt "$LOG_MAX_LINES" ]; then
      tail -n $((LOG_MAX_LINES/2)) "$LOG_FILE" > "$LOG_FILE.tmp"
      mv "$LOG_FILE.tmp" "$LOG_FILE"
    fi
    echo "[$(date)] CMD: $cmd" >>"$LOG_FILE"
  fi

  $DEBUG && echo -e "[DEBUG] Executing: $cmd"

  # We can't use `exec` anymore, as it replaces the script process.
  # We need to run the command, wait for it, and then check its exit code.
  bash -c "$cmd"
  local exit_status=$?

  # --- After-run logic ---
  # If the command succeeded (exit code 0) and we have a command to save...
  if [ $exit_status -eq 0 ] && [ -n "$cmd_to_save" ]; then
    echo # for spacing
    echo -e "${INFO}First successful run. Caching command...${RESET}"
    echo "CMD=\"$cmd_to_save\"" > "$RUNRC_FILE"
    echo -e "Saved command to ${OK}$RUNRC_FILE${RESET} for future use."
    echo -e "Use ${WARN}run --om \"new command\"${RESET} to overwrite."

    prompt_add_to_gitignore "$RUNRC_FILE"

  fi

  # Exit this script with the same status code as the command we ran
  exit $exit_status
}

# --- 6. Argument Parsing ---
FORCE=false; DETECT_ONLY=false; DEBUG=false; INIT_LANG=""; REPL_LANG=""; INCLUDE_PATHS=""; CLEAN=false; OVERWRITE_COMMAND="" # <-- ADD VARIABLE HERE
POSITIONAL=()
while (( "$#" )); do
  case $1 in
    -v|--version) echo "run $SCRIPT_VERSION"; exit;;
    --help)       show_help;;
    --list)       list_detections;;
    --langs)      list_languages;;
    --check)      check_environment;;
    --detect)     DETECT_ONLY=true; shift;;
    --force)      FORCE=true; shift;;
    --debug)      DEBUG=true; shift;;
    --log)        ENABLE_LOGGING=true; shift;;
    --clean)      CLEAN=true; shift;;
    --init|-i)    INIT_LANG="$2"; shift 2;;
    --repl)       REPL_LANG="$2"; shift 2;;
    --om|--overwrite-make) OVERWRITE_COMMAND="$2"; shift 2;; # <-- ADD THIS LINE
    --include)    INCLUDE_PATHS=":$2"; shift 2;;
    -* )          echo -e "${ERR}Unknown flag $1${RESET}"; exit 1;;
    * )           POSITIONAL+=("$1"); shift;;
  esac
done
set -- "${POSITIONAL[@]}"

# --- 7. Handle Generative & Clean Flags ---
$CLEAN && clean_artifacts
if [ -n "$INIT_LANG" ]; then
  if [ -n "${INIT_HANDLERS[$INIT_LANG]}" ]; then
    eval "${INIT_HANDLERS[$INIT_LANG]}"; exit
  else
    echo -e "${ERR}Unsupported init language: $INIT_LANG${RESET}"; exit 1
  fi
fi
if [ -n "$REPL_LANG" ]; then
  if [ -n "${REPL_HANDLERS[$REPL_LANG]}" ]; then
    eval "${REPL_HANDLERS[$REPL_LANG]}"
  else
    echo -e "${ERR}Unsupported REPL language: $REPL_LANG${RESET}"; exit 1
  fi
  exit
fi


# --- 7.5: Handle .runrc and Overwrite Mode ---

# Priority 1: Handle manual overwrite mode (--om)
if [ -n "$OVERWRITE_COMMAND" ]; then
    echo "CMD=\"$OVERWRITE_COMMAND\"" > "$RUNRC_FILE"
    echo -e "${OK}Saved new command to $RUNRC_FILE:${RESET}"
    echo -e "  ${CMD_COLOR}$OVERWRITE_COMMAND${RESET}"
    prompt_add_to_gitignore "$RUNRC_FILE"
    exit 0
fi

# Priority 2: If no file is passed, check for an existing .runrc
if [ -z "$1" ] && [ -f "$RUNRC_FILE" ]; then
    echo -e "${INFO}Using cached command from .runrc...${RESET}"
    source "$RUNRC_FILE"
    if [ -z "$CMD" ]; then
        echo -e "${ERR}.runrc found but 'CMD' variable is not set.${RESET}"
        echo -e "${WARN}You can reset it with: run --om \"your command\"${RESET}"
        exit 1
    fi
    # Use the existing execute_command function to run the cached command
    execute_command "$CMD"
fi


# --- 8. Plugin Loader ---
[ -d "$PLUGIN_DIR" ] && for plug in "$PLUGIN_DIR"/*.sh; do [ -f "$plug" ] && source "$plug"; done

# --- 9. Project Detection & Default File ---
if [ -z "$1" ]; then
  # plugin project handlers
  for marker in "${!PROJECT_HANDLERS[@]}"; do
    [ -f "$marker" ] && { "${PROJECT_HANDLERS[$marker]}"; exit; }
  done


  # Python projects
  if [ -f setup.py ] || [ -f pyproject.toml ]; then
    $DETECT_ONLY && { echo "DETECTED: Python project"; exit; }
    if [ -f manage.py ]; then
      execute_command "python3 manage.py runserver"
    else
      execute_command "python3 main.py"
    fi
  fi

# Node.js
if [ -f package.json ]; then
  pkg="npm"
  [ -f yarn.lock ]      && pkg="yarn"
  [ -f pnpm-lock.yaml ] && pkg="pnpm"
  $DETECT_ONLY && { echo "DETECTED: Node.js ($pkg)"; exit; }
  $FORCE && rm -rf node_modules
  $pkg install --silent

  # Pick main from package.json or default to index.js
  entry=$(node -p "require('./package.json').main || 'index.js'" 2>/dev/null)
  # If that file doesn't actually exist, fallback again
  [ ! -f "$entry" ] && entry="index.js"

  run="node \"$entry\""
  execute_command "$pkg run start --if-present || $run"
fi

# --- Java Project Detection (Manual, Maven, Gradle, Plain) ---
handled_java=false

# 1. Manual Java + JARs
if ! $handled_java && ls lib/*.jar &>/dev/null; then
  jars=$(echo lib/*.jar | tr ' ' ':')
  main_class=$(find src -name '*.java' -exec grep -l 'public static void main' {} + \
    | head -n1 | sed 's|src/||;s|\.java||;s|/|.|g')

  [ -z "$main_class" ] && {
    echo -e "${ERR}No Java main class found${RESET}"
    exit 1
  }

  echo -e "${INFO}Detected Java (manual JAR project):${RESET} $main_class"
  mkdir -p .run_tmp
  find src -name '*.java' > .run_tmp/sources.txt
  javac -d .run_tmp/classes -cp "$jars" @.run_tmp/sources.txt || {
    echo -e "${ERR}Manual Java compilation failed${RESET}"
    exit 1
  }
  execute_command "java -cp \"$jars:.run_tmp/classes\" $main_class"
  handled_java=true
fi

# 2. Maven
if ! $handled_java && [ -f pom.xml ]; then
  if command -v mvn &>/dev/null; then
    cmd="mvn compile exec:java"
    $FORCE && cmd="mvn clean && $cmd"
    $DETECT_ONLY && { echo "DETECTED: Maven"; exit; }
    execute_command "$cmd"
  else
    echo -e "${WARN}Maven not found. Falling back to manual javac...${RESET}"
    main_class=$(find src/main/java -name '*.java' -exec grep -l 'public static void main' {} + \
      | head -n1 | sed 's|src/main/java/||;s|\.java||;s|/|.|g')

    [ -z "$main_class" ] && {
      echo -e "${ERR}No Java main class found${RESET}"
      exit 1
    }

    echo -e "${INFO}Detected Java (Maven fallback):${RESET} $main_class"
    mkdir -p .run_tmp
    find src/main/java -name '*.java' > .run_tmp/sources.txt
    javac -d .run_tmp/classes @.run_tmp/sources.txt || {
      echo -e "${ERR}Java compilation failed${RESET}"
      exit 1
    }
    execute_command "java -cp .run_tmp/classes $main_class"
  fi
  handled_java=true
fi

# 3. Gradle
if ! $handled_java && ([ -f build.gradle ] || [ -f build.gradle.kts ]); then
  cmd="./gradlew run"
  $FORCE && cmd="./gradlew clean build run"
  $DETECT_ONLY && { echo "DETECTED: Gradle"; exit; }
  [ -f gradlew ] && chmod +x gradlew
  execute_command "$cmd"
  handled_java=true
fi

# 4. Fallback: Plain Java (src/main/java)
if ! $handled_java && [ -d src/main/java ]; then
  main_class=$(find src/main/java -name '*.java' -exec grep -l 'public static void main' {} + \
    | head -n1 | sed 's|src/main/java/||;s|\.java||;s|/|.|g')

  [ -z "$main_class" ] && {
    echo -e "${ERR}No Java main class found${RESET}"
    exit 1
  }

  echo -e "${INFO}Detected Java (manual fallback):${RESET} $main_class"
  mkdir -p .run_tmp
  find src/main/java -name '*.java' > .run_tmp/sources.txt
  javac -d .run_tmp/classes @.run_tmp/sources.txt || {
    echo -e "${ERR}Java compilation failed${RESET}"
    exit 1
  }
  execute_command "java -cp .run_tmp/classes $main_class"
  handled_java=true
fi


  # Cargo
  if [ -f Cargo.toml ]; then
    cmd="cargo run"
    $FORCE && cmd="cargo clean && $cmd"
    $DETECT_ONLY && { echo "DETECTED: Cargo"; exit; }
    execute_command "$cmd"
  fi

  # Makefile
  if [ -f Makefile ]; then
    cmd="make"
    $FORCE && cmd="make clean && make"
    $DETECT_ONLY && { echo "DETECTED: Makefile"; exit; }
    execute_command "$cmd"
  fi

# CMakeLists.txt
if [ -f CMakeLists.txt ]; then
  build_dir=.build
  cmd="cmake -S . -B $build_dir && cmake --build $build_dir"
  $FORCE && { rm -rf "$build_dir"; }

  $DETECT_ONLY && { echo "DETECTED: CMake"; exit; }
  # Build & then run the generated binary
  execute_command "$cmd && { \
    exe=\$(find \"$build_dir\" -maxdepth 1 -type f -executable | head -n1) && \
    echo -e \"${INFO}Running: \$exe${RESET}\" && \
    \"\$exe\"; \
  }"
fi

  # Smart-guess C/C++
  shopt -s nullglob
  cpps=(*.cpp *.cc); cs=(*.c); pys=(*.py)
  if (( ${#cpps[@]}>0 && ${#cs[@]}==0 && ${#pys[@]}==0 )); then
    set -- "${cpps[0]}"
  elif (( ${#cs[@]}>0 && ${#cpps[@]}==0 && ${#pys[@]}==0 )); then
    set -- "${cs[0]}"
  fi
  shopt -u nullglob

  # Fallback file list
  if [ -z "$1" ]; then
    for f in "${DEFAULT_FILE_ORDER[@]}"; do
      [ -f "$f" ] && { set -- "$f"; break; }
    done
  fi

  [ -z "$1" ] && { show_help; exit; }
fi

# --- 10. Single-File Runner ---
FILE="$1"
[ ! -f "$FILE" ] && { echo -e "${ERR}File not found: $FILE${RESET}"; exit 1; }

# plugin file handlers
for ext in "${!FILE_HANDLERS[@]}"; do
  if [[ "$FILE" == *"$ext" ]]; then
    "${FILE_HANDLERS[$ext]}" "$FILE"
    exit
  fi
done

# shebang scripts
first=$(head -n1 "$FILE")
if [[ "$first" == "#!"* ]]; then
  interp=$(echo "$first" | cut -c3- | awk '{print $1}')
  command -v "$interp" &>/dev/null || { echo -e "${ERR}Interpreter '$interp' not found.${RESET}"; exit 127; }
  $DETECT_ONLY && { echo "DETECTED: Shebang   $interp"; exit; }
  execute_command "chmod +x '$FILE' && '$FILE'"
fi

# compile & run helper
compile_and_run() {
  local cc="$1" src="$2" out="${2%.*}"
  $FORCE && rm -f "$out" 2>/dev/null
  eval "$cc '$src' -o '$out'$INCLUDE_PATHS" || { echo -e "${ERR}Compile failed${RESET}"; exit 1; }
  execute_command "./$out"
}

if [ -f "CMakeLists.txt" ]; then
  exe=$(find cmake-build-debug -maxdepth 1 -type f -executable | head -n1)
  if [ -n "$exe" ] && [ "$exe" -nt "$FILE" ]; then
    echo -e "${INFO}CMake project detected. Running built executable: $exe${RESET}"
    "$exe"
    exit 0
  else
    echo -e "${WARN}CMake project detected, but no recent build found. Falling back to compiling: $FILE${RESET}"
  fi
fi

case "$FILE" in
  *.cpp|*.cc) compile_and_run g++  "$FILE";;
  *.c)        compile_and_run gcc  "$FILE";;
  *.rs)       compile_and_run rustc "$FILE";;
  *.py)       $DETECT_ONLY && { echo "DETECTED: Python"; exit; }; execute_command "python3 '$FILE'";;
  *.js)       $DETECT_ONLY && { echo "DETECTED: JavaScript"; exit; }; execute_command "node '$FILE'";;
  *.ts)       command -v npx &>/dev/null || { echo -e "${ERR}npx missing${RESET}"; exit 1; }; $DETECT_ONLY && { echo "DETECTED: TypeScript"; exit; }; execute_command "npx ts-node $TS_NODE_FLAGS '$FILE'";;
  *.go)       $DETECT_ONLY && { echo "DETECTED: Go"; exit; }; execute_command "go run '$FILE'";;
  *.sh)       $DETECT_ONLY && { echo "DETECTED: Shell"; exit; }; chmod +x "$FILE"; execute_command "bash '$FILE'";;
  *.rb)       $DETECT_ONLY && { echo "DETECTED: Ruby"; exit; }; execute_command "ruby '$FILE'";;
  *.pl)       $DETECT_ONLY && { echo "DETECTED: Perl"; exit; }; execute_command "perl '$FILE'";;
  *.php)      $DETECT_ONLY && { echo "DETECTED: PHP"; exit; }; execute_command "php '$FILE'";;
  *.lua)      $DETECT_ONLY && { echo "DETECTED: Lua"; exit; }; execute_command "lua '$FILE'";;
  *.java)     $DETECT_ONLY && { echo "DETECTED: Java"; exit; }; execute_command "javac -cp .${INCLUDE_PATHS} '$FILE' && java -cp .${INCLUDE_PATHS} '${FILE%.java}'";;
  *.html)    
      dn=$(dirname "$FILE"); port=${PORT:-8000}
      command -v lsof &>/dev/null && while lsof -i :"$port" &>/dev/null; do ((port++)); done
      $DETECT_ONLY && { echo "DETECTED: HTML"; exit; }
      echo -e "${INFO}Serving http://localhost:$port${RESET}"
      execute_command "python3 -m http.server $port -d '$dn'";;
  *.md)       
      viewer="glow"; command -v glow &>/dev/null || viewer="less"
      $DETECT_ONLY && { echo "DETECTED: Markdown"; exit; }
      execute_command "$viewer '$FILE'";;
  *)          
      echo -e "${ERR}Unknown file type: $FILE${RESET}"
      exit 1;;
esac

exit 0
