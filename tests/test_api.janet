# Embedding API: typed values, registered commands, and error statuses.
(import ../src/cmdr :as cmdr)

(var failures 0)
(defn check [label ok]
  (unless ok
    (++ failures)
    (eprint "FAIL: " label)))
(defn raised "The error a line raises, or nil." [line]
  (try (do (cmdr/eval-line line) nil) ([e] e)))

# Typed values: lists keep their boundaries; no value is nil.
(check "list boundaries"
       (deep= ["A\nB" "" "A|B"] (cmdr/eval-line "list 'a\nb' '' 'a|b' | map upper")))
(check "nested lists" (deep= [["a"] ["b"]] (cmdr/eval-line "list a b | map list")))
(check "no value" (nil? (cmdr/eval-line "# comment")))
(check "text records" (= "a\nb\n" (cmdr/eval-line "text 'b\na\n' | sort")))

# Registered text commands receive text and may close over state.
(var calls 0)
(cmdr/register "bang" "bang TEXT" "Prefix !" (fn [s] (++ calls) (string "!" s)))
(check "text command" (deep= ["!a" "!b"] (cmdr/eval-line "list a b | map bang")))
(check "closure state" (= 2 calls))
(check "text command rejects lists"
       (string/find "expects text; use map or join" (raised "list a | bang")))

# Typed commands receive values as they are.
(cmdr/register "size" "size VALUE" "Count items or bytes" (fn [v] (string (length v))) true)
(check "typed command" (= "3" (cmdr/eval-line "list a b c | size")))

# Statuses: a predicate that does not hold is a mismatch; usage is an error.
(check "mismatch" (cmdr/mismatch? (raised "text a | equals b")))
(check "error" (let [e (raised "text a | equals")]
                 (and (string? e) (string/has-prefix? "usage:" e))))
(check "captured failure must be recovered" (= "division by zero" (raised "try 'div 1 0'")))
(check "recovered failure" (= "x" (cmdr/eval-line "try 'div 1 0' | recover x")))

# `as` and $name references are not part of the language.
(check "no as" (= "unknown command: as" (raised "text a | as x")))
(check "no named references" (string/has-prefix? "invalid reference '$x'" (raised "text $x")))

# Names: registered and defined commands are listed once, newest first.
(cmdr/eval-line "def shout 'upper | text !'")
(def names (cmdr/command-names))
(check "names" (and (= "shout" (first names)) (index-of "bang" names) (index-of "help" names)))
(check "names unique" (= (length names) (length (distinct names))))

# Application commands override the prelude.
(cmdr/register "upper" "upper TEXT" "Override" (fn [s] (string "U:" s)))
(check "override" (= "U:a" (cmdr/eval-line "text a | upper")))
(check "def keeps its definition-time upper" (= "A !" (cmdr/eval-line "text a | shout")))

(os/exit (if (zero? failures) 0 1))
