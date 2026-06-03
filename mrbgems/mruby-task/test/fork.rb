# test/fork.rb - Tests for Task#fork and per-task forked globals

# Basic isolation: $g reassignment inside forked task does not affect caller
assert("Task#fork: global isolation") do
  $fork_test_g = :original
  q = Task::Queue.new
  Task.current.fork do
    $fork_test_g = :inside
    q.push($fork_test_g)
  end
  Task.run
  result = q.pop
  assert_equal :inside, result
  assert_equal :original, $fork_test_g
end

# Forked child sees parent's globals at spawn time
assert("Task#fork: child inherits parent globals at spawn") do
  $fork_inherit_g = :parent_value
  q = Task::Queue.new
  Task.current.fork do
    q.push($fork_inherit_g)
  end
  Task.run
  assert_equal :parent_value, q.pop
end

# Two sibling forked tasks do not interfere with each other
assert("Task#fork: sibling isolation") do
  $fork_sibling_g = :root
  q1 = Task::Queue.new
  q2 = Task::Queue.new
  Task.current.fork do
    $fork_sibling_g = :task1
    Task.pass
    q1.push($fork_sibling_g)
  end
  Task.current.fork do
    $fork_sibling_g = :task2
    Task.pass
    q2.push($fork_sibling_g)
  end
  Task.run
  assert_equal :task1, q1.pop
  assert_equal :task2, q2.pop
  assert_equal :root, $fork_sibling_g
end

# Shared heap objects remain shared (shallow fork semantics)
assert("Task#fork: shared objects remain shared") do
  shared_arr = [1, 2, 3]
  $fork_arr_g = shared_arr
  q = Task::Queue.new
  Task.current.fork do
    $fork_arr_g << 4   # mutates shared object, not a rebinding
    q.push($fork_arr_g.object_id)
  end
  Task.run
  q.pop
  assert_equal [1, 2, 3, 4], shared_arr
end

# Task.new (non-forked) still shares globals
assert("Task.new: still shares real globals") do
  $fork_shared = :before
  q = Task::Queue.new
  Task.new do
    $fork_shared = :written
    q.push(:done)
  end
  Task.run
  q.pop
  assert_equal :written, $fork_shared
end

# $stdout redirection pattern (the primary use case)
assert("Task#fork: $stdout redirect via Task::Queue") do
  q = Task::Queue.new
  Task.current.fork do
    $stdout = q
    $stdout.push("piped")
  end
  Task.run
  assert_equal "piped", q.pop
end
