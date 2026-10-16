# cmdr: a pipeline language for commands with positional arguments.
#
# A pipeline compiles to a Janet closure of $1..$N. Command names resolve when
# a pipeline is compiled, so `def` bodies keep the commands they were defined
# with. Pipeline strings passed to try/each/keep/not/when compile when run.
#
# Values: text is a string, a list is a tuple, no value is nil, and a captured
# failure is {:failure MSG}. Errors are strings (status 1) or {:mismatch MSG}
# for a predicate that does not hold.

### Values and errors

(defn failure [msg] {:failure msg})
(defn failure? [v] (and (struct? v) (truthy? (v :failure))))

(defn failure-in "Message of the first captured failure inside v, or nil." [v]
  (cond (failure? v) (v :failure)
        (tuple? v) (some failure-in v)))

(defn mismatch [msg] (error {:mismatch msg}))
(defn mismatch? [e] (and (struct? e) (truthy? (e :mismatch))))
(defn message [e] (if (mismatch? e) (e :mismatch) (string e)))
(defn- prefix "Re-raise e as `p: e`, keeping a mismatch a mismatch." [e p]
  (def msg (string p ": " (message e)))
  (error (if (mismatch? e) {:mismatch msg} msg)))

(defn require-text [v]
  (if-let [f (failure-in v)] (error f))
  (if (string? v) v (error "expected text")))

(defn as-list "Lists pass through; text splits into newline-terminated records." [v]
  (if (tuple? v) v
    (let [s (require-text v)]
      (if (empty? s) []
        (let [parts (string/split "\n" s)]
          (tuple/slice parts 0 (if (string/has-suffix? "\n" s) -2 -1)))))))

(defn render "Text as-is; lists as newline-terminated items." [v]
  (cond (nil? v) nil
        (failure? v) (error (v :failure))
        (tuple? v) (string ;(map |(string (render $) "\n") v))
        v))

(defn- like-input [input items]
  (if (string? input) (render items) items))

(defn- natural [v]
  (def s (require-text v))
  (if (empty? s) (error "expected a nonnegative integer"))
  # Doubles are exact below 2^53; 15 digits stay under it.
  (unless (and (peg/match ~(* (some (range "09")) -1) s) (< (length s) 16))
    (error (string "invalid nonnegative integer: " s)))
  (scan-number s))

### Registry: layers searched newest first.

(defn- layer [parent cmds]
  @{:cmds cmds :by-name (tabseq [c :in cmds] (c :name) c) :parent parent})

(defn lookup [reg name]
  (var r reg)
  (var found nil)
  (while (and r (not found))
    (set found (get-in r [:by-name name]))
    (set r (r :parent)))
  found)

(defn- visible "Commands of reg in lookup order, without shadowed ones." [reg]
  (def out @[])
  (var r reg)
  (while r
    (each c (r :cmds) (if (= (lookup reg (c :name)) c) (array/push out c)))
    (set r (r :parent)))
  out)

(var- registry "Registry for names resolved at run time." nil)
(var- top "Session registry; `def` pushes layers onto it." nil)
(var- depth 0)
(def max-depth "Composed commands may nest this deep." 128)

(defn- invoke [cmd argv]
  (if (and (> (length argv) 0) (failure? (in argv 0)) (not (cmd :handles-failure)))
    (in argv 0)
    (do
      (if (>= depth max-depth)
        (error (string "command '" (cmd :name) "' is nested too deeply "
                       "(possible cycle in composed commands)")))
      (++ depth)
      (def result ((cmd :fn) ;argv))
      (-- depth)
      result)))

# Janet's try and defer each run their body in a new fiber, so state is
# restored only where errors are caught, not on every call.
(defn- attempt "Call thunk; on error restore interpreter state and call handler." [thunk handler]
  (def [d r] [depth registry])
  (try (thunk) ([e] (set depth d) (set registry r) (handler e))))

### Parser

(def- ws ~(set " \t\n\v\f\r"))

(defn- ref-word [w]
  (unless (and (peg/match ~(* (range "19") (any (range "09")) -1) w) (< (length w) 16))
    (error (string "invalid reference '$" w "': only $1..$N are supported; "
                   "quote '$' for literal text")))
  [:ref (scan-number w) (string "$" w)])

(def- grammar
  (peg/compile
    ~{:ws ,ws
      :unclosed (error (constant "unclosed quote"))
      :unfinished (error (constant "unfinished escape"))
      :sq (* "'" (<- (any (if-not "'" 1))) (+ "'" :unclosed))
      :dq (* "\"" (any (+ (* "\\" (+ (* "n" (constant "\n")) (* "t" (constant "\t"))
                                    (<- 1) :unfinished))
                          (<- (some (if-not (set "\"\\") 1)))))
             (+ "\"" :unclosed))
      :esc (* "\\" (+ (<- 1) :unfinished))
      :plain (<- (some (if-not (+ :ws (set "|'\"\\")) 1)))
      :word (/ (some (+ :sq :dq :esc :plain)) ,(fn [& parts] [:word (string ;parts)]))
      :ref (/ (* "$" (<- (any (if-not (+ :ws "|") 1)))) ,ref-word)
      :bar (* "|" (constant [:bar]))
      :main (* (any (+ :ws :bar :ref :word)) -1)}))

(defn- blank-or-comment? [s] (truthy? (peg/match ~(* (any ,ws) (+ -1 "#")) s)))

(defn parse "Split line into stages of [:word TEXT] and [:ref N SOURCE] tokens." [line]
  (if (peg/match ~(* (any ,ws) "#") line) (break @[]))
  (def stages @[@[]])
  (each tok (peg/match grammar line)
    (if (= (tok 0) :bar)
      (do (if (empty? (last stages)) (error "expected a command before '|'"))
          (array/push stages @[]))
      (array/push (last stages) tok)))
  (if (and (> (length stages) 1) (empty? (last stages)))
    (error "expected a command after '|'"))
  (filter |(not (empty? $)) stages))

### Compiler

(defn- check-arity [argc highest name]
  (when (if (pos? highest) (not= argc highest) (> argc 1))
    (error
      (cond
        (and name (zero? highest)) (string "pipeline command '" name "' accepts at most one value")
        name (string/format "command '%s' expects %d argument%s, got %d"
                            name highest (if (= highest 1) "" "s") argc)
        (zero? highest) (string/format "pipeline without references accepts at most one value (%d given)" argc)
        (string/format "reference $%d has no argument (%d given)" highest argc)))))

(defn- run-stage [cmd piped cur args name]
  (if (and piped (nil? cur))
    (error (string "command '" (cmd :name) "' received no pipeline value")))
  (def argv (if piped [cur ;args] args))
  (if name
    (attempt |(invoke cmd argv) |(prefix $ name))
    (invoke cmd argv)))

(defn compile-pipeline
  "Compile line against reg into {:fn F :highest N}; F takes $1..$N. name
  labels errors from a `def` body."
  [line reg &opt name]
  (def stages (parse line))
  (var highest 0)
  # The current value is named while it is $1 or a lone reference; a stage
  # that names it receives it only where named, not as its first argument.
  (var current 1)
  # Each step maps (params cur have) to the next current value.
  (def steps
    (seq [stage :in stages]
      (each [kind n] stage (if (= kind :ref) (set highest (max highest n))))
      (def [kind head source] (stage 0))
      (cond
        (and (= kind :ref) (= 1 (length stage)))
        (let [i (dec head)]
          (set current head)
          (fn [params cur have] (if (and have (failure? cur)) cur (params i))))
        (= kind :ref) (error (string "command name cannot be a reference: " source))
        (let [cmd (or (lookup reg head) (error (string "unknown command: " head)))
              args (slice stage 1)
              piped (not (some (fn [[k n]] (and (= k :ref) (= n current))) args))
              refs (some (fn [[k]] (= k :ref)) args)
              words (map 1 args)]
          (set current 0)
          (fn [params cur have]
            (run-stage cmd (and have piped) cur
                       (if refs (map (fn [[k x]] (if (= k :ref) (params (dec x)) x)) args) words)
                       name))))))
  (def lone-first (and (not (empty? stages)) (= 1 (length (stages 0)))
                       (= :ref (get-in stages [0 0 0]))))
  {:highest highest
   :fn (fn [& params]
         (check-arity (length params) highest name)
         (var have (and (not lone-first) (pos? (length params))))
         (var cur (if have (params 0)))
         # Every step leaves a current value, even when it keeps a failure.
         (each step steps
           (set cur (step params cur have))
           (set have true))
         (if-let [f (failure-in cur)] (error f) cur))})

(def- cache @{})

(defn- body "Compile a pipeline argument in the active registry, caching errors too." [v]
  (def src (require-text v))
  (def key [registry src])
  (or (cache key)
      (set (cache key)
           (try (compile-pipeline src registry) ([e] {:error (message e)})))))

(defn- body-fn [b] (if-let [e (b :error)] (error e) (b :fn)))

(defn- check-body "Compile and arity-check a body before any item runs." [context v argc]
  (def b (body v))
  (try (do (body-fn b) (check-arity argc (b :highest) nil))
       ([e] (error (string context ": " (message e)))))
  (b :fn))

### Commands

(defn- arity [usage n argv]
  (if (not= n (length argv)) (error (string "usage: " usage))))

(defn text-command
  "A command whose arguments must all be text. f returns text, or nil for no value."
  [name usage description f]
  {:name name :usage usage :doc description
   :fn (fn [& argv]
         (f ;(map |(if (string? $) $
                     (error (or (failure-in $)
                                (string "command '" name "' expects text; use map or join for lists"))))
                  argv)))})

(defn- number [s]
  (def x (and (peg/match ~(* (? (set "+-")) (+ (* (some (range "09")) (? (* "." (any (range "09")))))
                                               (* "." (some (range "09"))))
                             (? (* (set "eE") (? (set "+-")) (some (range "09")))) -1) s)
              (scan-number s)))
  (if (and x (< (math/abs x) math/inf)) x (error (string "invalid finite number: " s))))

(defn- arithmetic [op exact]
  (fn [& args]
    (if (if exact (not= 2 (length args)) (< (length args) 2))
      (error (string "arithmetic expects " (if exact "exactly two" "at least two") " numbers")))
    (var v (number (args 0)))
    (each a (slice args 1)
      (def x (number a))
      (if (and (= op /) (zero? x)) (error "division by zero"))
      (set v (op v x))
      (unless (< (math/abs v) math/inf) (error "arithmetic overflow")))
    (string/format "%.17g" v)))

(defn- comparison [name op phrase]
  (fn [& args]
    (if (not= 2 (length args)) (error (string "usage: " name " NUMBER LIMIT")))
    (def [a b] (map number args))
    (if (op a b) (args 0)
      (mismatch (string name ": " (args 0) " is not " phrase " " (args 1))))))

# Janet does not expose errno; infer the common cases.
(defn- open-error [path]
  (case (os/stat path :mode)
    nil "No such file or directory"
    :directory "Is a directory"
    "Permission denied"))

(defn- read-file [& args]
  (arity "read PATH" 1 args)
  (def path (args 0))
  (if (= :directory (os/stat path :mode)) (error (string "read " path ": Is a directory")))
  (def f (or (file/open path :rb) (error (string "read " path ": " (open-error path)))))
  (def data (defer (file/close f) (file/read f :all)))
  (if (string/find "\0" data) (error "read: binary data is not supported"))
  (string data))

# Temporary files rather than pipes: no deadlock when the child writes
# before reading, and no SIGPIPE when it never reads.
(defn- execute [input args]
  (if (or (empty? args) (empty? (args 0))) (error "expected an executable name"))
  (def name (args 0))
  (with [in (file/temp)]
    (with [out (file/temp)]
      (file/write in input)
      (file/seek in :set 0)
      (def code (try (os/execute args :p {:in in :out out})
                     ([e] (error (string "run " name ": " (last (string/split ": " e)))))))
      (if (not= code 0) (error (string/format "run %s: exited with status %d" name code)))
      (file/seek out :set 0)
      (def data (string (or (file/read out :all) "")))
      (if (string/find "\0" data) (error (string "run " name ": binary stdout is not supported")))
      data)))

(defn- unary [name description f]
  (def usage (string name " TEXT"))
  (text-command name usage description (fn [& a] (arity usage 1 a) (f (a 0)))))

(def- prelude
  (layer nil
    [(text-command "text" "text [ARG ...]" "Join arguments with spaces" (fn [& a] (string/join a " ")))
     (unary "upper" "Uppercase text" string/ascii-upper)
     (unary "lower" "Lowercase text" string/ascii-lower)
     (unary "trim" "Trim surrounding whitespace" |(string/trim $ " \t\n\v\f\r"))
     (unary "length" "Count bytes" |(string (length $)))
     (text-command "replace" "replace TEXT OLD NEW" "Replace all literal matches (OLD nonempty)"
                   (fn [& a]
                     (if (or (not= 3 (length a)) (empty? (a 1)))
                       (error "usage: replace TEXT NONEMPTY_SEARCH REPLACEMENT"))
                     (string (string/replace-all (a 1) (a 2) (a 0)))))
     (text-command "add" "add A B [NUMBER ...]" "Add numbers" (arithmetic + false))
     (text-command "sub" "sub A B" "Subtract B from A" (arithmetic - true))
     (text-command "mul" "mul A B [NUMBER ...]" "Multiply numbers" (arithmetic * false))
     (text-command "div" "div A B" "Divide A by B" (arithmetic / true))
     (text-command "lt" "lt NUMBER LIMIT" "Return NUMBER if less than LIMIT" (comparison "lt" < "less than"))
     (text-command "gt" "gt NUMBER LIMIT" "Return NUMBER if greater than LIMIT" (comparison "gt" > "greater than"))
     (text-command "read" "read PATH" "Read a text file" read-file)
     (text-command "print" "print TEXT" "Print TEXT plus newline, returning TEXT"
                   (fn [& a] (arity "print TEXT" 1 a) (print (a 0)) (file/flush stdout) (a 0)))
     (text-command "run" "run PROGRAM [ARG ...]" "Run an executable, returning stdout"
                   (fn [& a] (if (empty? a) (error "usage: run PROGRAM [ARG ...]")) (execute "" a)))
     (text-command "run-in" "run-in TEXT PROGRAM [ARG ...]" "Run with TEXT on stdin, returning stdout"
                   (fn [& a] (if (< (length a) 2) (error "usage: run-in TEXT PROGRAM [ARG ...]"))
                     (execute (a 0) (slice a 1))))]))

(defn- collection [name usage description op]
  (def extra (string/find " N" usage))
  {:name name :usage usage :doc description
   :fn (fn [& argv]
         (def filtering (= name "filter"))
         (unless (= (length argv) (if (or extra filtering) 2 1))
           (error (string "usage: " name " LIST_OR_LINES" (cond filtering " SUBSTRING" extra " N" ""))))
         (def arg (cond filtering (require-text (argv 1)) extra (natural (argv 1))))
         (def items (as-list (argv 0)))
         (if (= name "count")
           (do (if-let [f (failure-in items)] (error f)) (string (length items)))
           (do (unless extra (each i items (require-text i)))
               (like-input (argv 0) (tuple ;(op items arg))))))})

(defn- uniq [items _]
  (seq [[k i] :pairs items :unless (and (pos? k) (= i (items (- k 1))))] i))

(defn- per-item [keep]
  (def name (if keep "keep" "each"))
  (fn [& argv]
    (if (< (length argv) 2) (error (string "usage: " name " LIST_OR_LINES PIPELINE [ARG ...]")))
    (def extra (slice argv 2))
    (def f (check-body name (argv 1) (+ 1 (length extra))))
    (def items (as-list (argv 0)))
    (def out @[])
    (var n 0)
    (defn fail [msg] (error (string/format "%s item %d: %s" name n msg)))
    (if keep
      (each item items
        (++ n)
        (if-let [fl (failure-in item)] (fail fl))
        (attempt |(do (f item ;extra) (array/push out item))
                 |(unless (mismatch? $) (fail (message $)))))
      (attempt |(each item items
                  (++ n)
                  (def r (f item ;extra))
                  (if (nil? r) (error "pipeline returned no value"))
                  (array/push out r))
               |(fail (message $))))
    (if keep (like-input (argv 0) (tuple ;out)) (tuple ;out))))

(defn- not-cmd [& argv]
  (if (< (length argv) 2) (error "usage: not VALUE PIPELINE [ARG ...]"))
  (def extra (slice argv 2))
  (def f (check-body "not" (argv 1) (+ 1 (length extra))))
  (def held (attempt |(do (f (argv 0) ;extra) true)
                     |(if (mismatch? $) false (error (string "not: " (message $))))))
  (if held (mismatch (string "not: '" (argv 1) "' succeeded")) (argv 0)))

# Each body receives only the parameters it references, so bodies that
# ignore shared arguments still pass strict arity.
(defn- when-cmd [& argv]
  (if (< (length argv) 4) (error "usage: when VALUE COND THEN ELSE [ARG ...]"))
  (def params [(argv 0) ;(slice argv 4)])
  (def labels ["COND" "THEN" "ELSE"])
  (def bodies (seq [i :range [0 3]]
                (def b (body (argv (+ i 1))))
                (try (body-fn b) ([e] (error (string "when " (labels i) ": " (message e)))))
                b))
  (def highest (max ;(map |($ :highest) bodies)))
  (def used (max highest 1))
  (cond
    (> highest (length params))
    (error (string/format "when: reference $%d has no argument (%d given)" highest (length params)))
    (> (length params) used)
    (let [n (- (length params) used)]
      (error (string/format "when: %d argument%s unused" n (if (= n 1) "" "s")))))
  (defn run [b] ((b :fn) ;(slice params 0 (max 1 (b :highest)))))
  (def held (attempt |(do (run (bodies 0)) true)
                     |(if (mismatch? $) false (error (string "when COND: " (message $))))))
  (run (bodies (if held 1 2))))

(defn- predicate [name usage description test]
  {:name name :usage usage :doc description
   :fn (fn [& argv]
         (if (not= 2 (length argv)) (error (string "usage: " usage)))
         (def [s x] (map require-text argv))
         (if-let [msg (test s x)] (mismatch (string name ": " msg)) s))})

(defn- path-part [directory]
  (fn [& argv]
    (arity (if directory "dirname PATH" "basename PATH") 1 argv)
    (def path (require-text (argv 0)))
    (def slash (chr "/"))
    (var end (length path))
    (if (zero? end) (break "."))
    (while (and (> end 1) (= (path (- end 1)) slash)) (-- end))
    (if (and (= end 1) (= (path 0) slash)) (break "/"))
    (var start end)
    (while (and (pos? start) (not= (path (- start 1)) slash)) (-- start))
    (if (not directory) (break (string/slice path start end)))
    (while (and (> start 1) (= (path (- start 1)) slash)) (-- start))
    (if (pos? start) (string/slice path 0 start) ".")))

(defn- files-cmd [& argv]
  (if (> (length argv) 1) (error "usage: files [DIRECTORY]"))
  (def path (if (empty? argv) "." (require-text (argv 0))))
  (case (os/stat path :mode)
    nil (error (string "files " path ": No such file or directory"))
    :directory nil
    (error (string "files " path ": Not a directory")))
  (def dir (if (or (empty? path) (string/has-suffix? "/" path)) path (string path "/")))
  # os/stat follows symlinks, so dangling links have no mode and are skipped.
  (tuple ;(sort (seq [n :in (os/dir path) :let [full (string dir n)]
                      :when (= :file (os/stat full :mode))]
                  full))))

(defn- slicing [name usage description]
  {:name name :usage usage :doc description
   :fn (fn [& argv]
         (arity usage (if (= name "slice") 3 2) argv)
         (def s (require-text (argv 0)))
         (def from (min (natural (argv 1)) (length s)))
         (case name
           "take" (string/slice s 0 from)
           "drop" (string/slice s from)
           (string/slice s from (min (length s) (+ from (natural (argv 2)))))))})

(defn- save [name mode description]
  (def usage (string name " VALUE PATH"))
  {:name name :usage usage :doc description
   :fn (fn [& argv]
         (arity usage 2 argv)
         (def path (require-text (argv 1)))
         (def text (or (render (argv 0)) (error "cannot write a missing value")))
         (def f (or (file/open path mode) (error (string name " " path ": " (open-error path)))))
         (defer (file/close f) (file/write f text))
         (argv 0))})

(defn- try-cmd [& argv]
  (def argc (length argv))
  (unless (or (= argc 1) (= argc 2)) (error "usage: try [INPUT] 'PIPELINE'"))
  (def b (body (last argv)))
  (attempt |(let [f (body-fn b)] (if (= argc 2) (f (argv 0)) (f)))
           |(failure (message $))))

(defn- map-cmd [& argv]
  (if (< (length argv) 2) (error "usage: map LIST_OR_LINES COMMAND [ARG ...]"))
  (def name (require-text (argv 1)))
  (def cmd (or (lookup registry name) (error (string "unknown command: " name))))
  (def rest (slice argv 2))
  (def items (as-list (argv 0)))
  (def out @[])
  (attempt |(each item items
              (def r (invoke cmd [item ;rest]))
              (if (nil? r) (error "command returned no value"))
              (array/push out r))
           |(error (string/format "map item %d (%s): %s" (+ 1 (length out)) name (message $))))
  (tuple ;out))

(def- essentials
  (layer prelude
    [{:name "list" :usage "list [VALUE ...]" :doc "Construct an explicit list" :fn tuple}
     {:name "lines" :usage "lines LIST_OR_LINES" :doc "Split text into a list of lines"
      :fn (fn [& argv] (arity "lines LIST_OR_LINES" 1 argv) (as-list (argv 0)))}
     {:name "split" :usage "split TEXT SEPARATOR" :doc "Split into an explicit list"
      :fn (fn [& argv]
            (arity "split TEXT NONEMPTY_SEPARATOR" 2 argv)
            (def [s sep] (map require-text argv))
            (if (empty? sep) (error "split: separator must be nonempty"))
            (if (empty? s) [] (tuple ;(string/split sep s))))}
     {:name "join" :usage "join LIST_OR_LINES SEPARATOR" :doc "Join text items"
      :fn (fn [& argv]
            (arity "join LIST_OR_LINES SEPARATOR" 2 argv)
            (def sep (require-text (argv 1)))
            (string/join (map require-text (as-list (argv 0))) sep))}
     (collection "sort" "sort LIST_OR_LINES" "Sort text items by byte order" (fn [items _] (sorted items)))
     (collection "uniq" "uniq LIST_OR_LINES" "Remove adjacent duplicate text items" uniq)
     (collection "head" "head LIST_OR_LINES N" "Keep the first N items"
                 (fn [items n] (tuple/slice items 0 (min n (length items)))))
     (collection "tail" "tail LIST_OR_LINES N" "Keep the last N items"
                 (fn [items n] (tuple/slice items (max 0 (- (length items) n)))))
     (collection "filter" "filter LIST_OR_LINES TEXT" "Keep text items containing TEXT"
                 (fn [items sub] (filter |(or (empty? sub) (string/find sub $)) items)))
     (collection "count" "count LIST_OR_LINES" "Count items" nil)
     {:name "map" :usage "map LIST_OR_LINES COMMAND [ARG ...]" :doc "Apply a command to each item" :fn map-cmd}
     {:name "each" :usage "each LIST_OR_LINES PIPELINE [ARG ...]" :doc "Run a pipeline per item ($1 = item)"
      :fn (per-item false)}
     {:name "keep" :usage "keep LIST_OR_LINES PIPELINE [ARG ...]" :doc "Keep items whose pipeline succeeds"
      :fn (per-item true)}
     {:name "not" :usage "not VALUE PIPELINE [ARG ...]" :doc "Return VALUE if the pipeline fails" :fn not-cmd}
     {:name "when" :usage "when VALUE COND THEN ELSE [ARG ...]" :doc "Run THEN if COND holds, else ELSE"
      :fn when-cmd}
     {:name "nonempty" :usage "nonempty VALUE" :doc "Return VALUE unless empty text or list"
      :fn (fn [& argv]
            (arity "nonempty VALUE" 1 argv)
            (def v (argv 0))
            (cond (= v "") (mismatch "nonempty: empty text")
                  (= v []) (mismatch "nonempty: empty list")
                  v))}
     (predicate "equals" "equals TEXT EXPECTED" "Return TEXT if equal to EXPECTED"
                |(if (not= $0 $1) (string "text differs from '" $1 "'")))
     (predicate "contains" "contains TEXT SUBSTRING" "Return TEXT if it contains SUBSTRING"
                |(if (not (or (empty? $1) (string/find $1 $0))) (string "'" $1 "' not found")))
     {:name "files" :usage "files [DIRECTORY]" :doc "List regular files, sorted by path" :fn files-cmd}
     {:name "basename" :usage "basename PATH" :doc "Return the final path component" :fn (path-part false)}
     {:name "dirname" :usage "dirname PATH" :doc "Return the parent path" :fn (path-part true)}
     (slicing "take" "take TEXT N" "Take the first N bytes")
     (slicing "drop" "drop TEXT N" "Drop the first N bytes")
     (slicing "slice" "slice TEXT START LENGTH" "Extract bytes at a zero-based offset")
     (save "append" :ab "Append to a file, returning VALUE")
     (save "write" :wb "Replace a file, returning VALUE")
     (save "tee" :wb "Replace a file, returning VALUE")
     {:name "try" :usage "try [INPUT] 'PIPELINE'" :doc "Capture a pipeline failure" :fn try-cmd}
     {:name "recover" :usage "recover VALUE FALLBACK" :doc "Substitute FALLBACK after try fails"
      :handles-failure true
      :fn (fn [& argv] (arity "recover VALUE FALLBACK" 2 argv)
            (if (failure? (argv 0)) (argv 1) (argv 0)))}]))

### Application layer: help, def, and registered commands.

(defn- help-cmd [& argv]
  (if (not (empty? argv)) (error "usage: help"))
  (def rows (map |[(or ($ :usage) ($ :name)) ($ :doc)] (visible registry)))
  (def width (max ;(map |(length ($ 0)) rows)))
  (string
    ;(seq [[usage description] :in rows]
       (if (and description (not (empty? description)))
         (string usage (string/repeat " " (- width (length usage))) "  " description "\n")
         (string usage "\n")))
    "Compose with |; the returned value becomes the next command's first argument.\n"
    "Quote spaces and pipes; $1..$N are arguments.\n"))

(defn- def-cmd [& argv]
  (unless (and (= 2 (length argv)) (all string? argv)) (error "usage: def NAME PIPELINE"))
  (def [name src] argv)
  (if (or (empty? name) (index-of (name 0) [(chr "$") (chr "#")])
          (peg/match ~(to (+ ,ws (set "|'\"\\"))) name))
    (error (string "def: invalid name '" name "'")))
  (if (= name "def") (error "def: cannot redefine def"))
  (def scope top)
  (def f ((try (compile-pipeline src scope name)
               ([e] (error (string "def " name ": " (message e)))))
           :fn))
  (if (blank-or-comment? src) (error (string "def " name ": empty pipeline")))
  # Run-time names in the body (map, try) also resolve in its own scope.
  (def cmd {:name name
            :doc (string "def: " (string/join (peg/match ~(any (+ (some ,ws) (<- (some (if-not ,ws 1))))) src) " "))
            :fn (fn [& args]
                  (def saved registry)
                  (set registry scope)
                  (def result (f ;args))
                  (set registry saved)
                  result)})
  (set top (layer top [cmd]))
  (set registry top)
  nil)

(def- application
  (layer essentials
    @[{:name "help" :usage "help" :doc "Show commands" :fn help-cmd}
      {:name "def" :usage "def NAME PIPELINE" :doc "Define a pipeline command; it may shadow others" :fn def-cmd}]))
(set top application)

(defn register
  "Add an application command; it overrides the prelude. f receives text
  arguments, or any values when typed is true, and returns a value or nil."
  [name usage description f &opt typed]
  (def cmd (if typed {:name name :usage usage :doc description :fn f} (text-command name usage description f)))
  (array/push (application :cmds) cmd)
  (put (application :by-name) name cmd))

(defn command-names "Active command names in lookup order." []
  (map |($ :name) (visible top)))

(defn eval-line "Evaluate one pipeline; return its value or raise its error." [line]
  (set registry top)
  (set depth 0)
  (((compile-pipeline line top) :fn)))

### Driver

(var- editor "Set by the host: {:read (fn [prompt names]) :remember (fn [line])}." nil)

(defn use-editor
  "Read interactive lines with (read prompt names), which returns a line, nil
  at end of input, or :interrupt; record pipelines with (remember pipeline)."
  [read remember]
  (set editor {:read read :remember remember}))

(defn- evaluate [line where]
  (try
    (do (def text (render (eval-line line)))
        (when text (prin text) (unless (string/has-suffix? "\n" text) (print)))
        (file/flush stdout)
        0)
    ([e] (file/flush stdout) (eprint "cmdr: " (or where "") (message e)) 1)))

(defn- continues? [s]
  (def t (string/trimr s " \t\n\v\f\r"))
  (and (string/has-suffix? "|" t) (not (string/has-suffix? "\\|" t)) (not (blank-or-comment? s))))

(defn- run-stream
  "One pipeline per line; a trailing `|` continues. A script stops at its
  first failure, since later lines may depend on earlier ones."
  [in path]
  (def interactive (and (not path) (os/isatty stdin)))
  (def ed (if interactive editor))
  (def pipeline @"")
  (var [status number start used] [0 0 0 false])
  (while true
    (def prompt-text (if used "...> " "cmdr> "))
    (def line (if ed ((ed :read) prompt-text (command-names))
                (do (when interactive (prin prompt-text) (file/flush stdout))
                    (file/read in :line))))
    (var ready true)
    (cond
      (= line :interrupt) (do (buffer/clear pipeline) (set used false) (set ready false))
      line (do (++ number)
               (if (and used (blank-or-comment? line))
                 (set ready false)
                 (do (unless used (set start number))
                     (buffer/push pipeline line)
                     (set used true)
                     (if (continues? pipeline) (set ready false))))))
    (when ready
      (unless used (break))
      (def src (string pipeline))
      (buffer/clear pipeline)
      (set used false)
      (if (and interactive (index-of (string/trim src) ["quit" "exit"])) (break))
      (if ed ((ed :remember) src))
      (when (not= 0 (evaluate src (if path (string path ":" start ": "))))
        (set status 1)
        (if path (break)))
      (unless line (break))))
  status)

(defn main [& args]
  (def code
    (cond
      (and (= 2 (length args)) (= "-c" (args 0))) (evaluate (args 1) nil)
      (empty? args) (run-stream stdin nil)
      (or (not= 1 (length args)) (string/has-prefix? "-" (args 0)))
      (do (eprint "usage: cmdr [-c PIPELINE | FILE]") 2)
      (if-let [f (file/open (args 0) :r)]
        (defer (file/close f) (run-stream f (args 0)))
        (do (eprint "cmdr: " (args 0) ": " (open-error (args 0))) 1))))
  (file/flush stdout)
  (os/exit code))
