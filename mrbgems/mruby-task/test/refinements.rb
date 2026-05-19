module Ext1
  refine String do
    def shout
      upcase + "!!"
    end
  end
end

module Ext2
  refine String do
    def shout
      upcase + "??"
    end
  end
end

module IntExt
  refine Integer do
    def double
      self * 2
    end
  end
end

assert("Basic refinement: refined method works after using") do
  r = nil
  done = false
  Task.new { Task.current.using Ext1; r = "hello".shout; done = true }
  while !done; Task.pass; end
  assert_equal "HELLO!!", r
end

assert("Isolation: task without using cannot see refined method") do
  r = nil
  done = false
  Task.new do
    begin
      "hello".shout
      r = false
    rescue NoMethodError
      r = true
    end
    done = true
  end
  while !done; Task.pass; end
  assert_true r
end

assert("unusing removes refinement") do
  r = nil
  done = false
  Task.new do
    Task.current.using Ext1
    ok_before = "hello".shout == "HELLO!!"
    Task.current.unusing Ext1
    begin
      "hello".shout
      ok_after = false
    rescue NoMethodError
      ok_after = true
    end
    r = ok_before && ok_after
    done = true
  end
  while !done; Task.pass; end
  assert_true r
end

assert("Multiple refinements LIFO: later using wins") do
  r = nil
  done = false
  Task.new do
    Task.current.using Ext1
    Task.current.using Ext2
    r = "hello".shout
    done = true
  end
  while !done; Task.pass; end
  assert_equal "HELLO??", r
end

assert("Integer refinement works") do
  r = nil
  done = false
  Task.new { Task.current.using IntExt; r = 21.double; done = true }
  while !done; Task.pass; end
  assert_equal 42, r
end

assert("using non-module raises TypeError") do
  r = nil
  done = false
  Task.new do
    begin
      Task.current.using "not a module"
      r = false
    rescue TypeError
      r = true
    end
    done = true
  end
  while !done; Task.pass; end
  assert_true r
end

assert("Spawn copy: child inherits parent refinements") do
  r = nil
  parent_done = false
  child_done = false
  Task.new do
    Task.current.using Ext1
    Task.new do
      begin
        r = "hello".shout
      rescue NoMethodError
        r = :no_method
      end
      child_done = true
    end
    parent_done = true
  end
  while !parent_done || !child_done; Task.pass; end
  assert_equal "HELLO!!", r
end

assert("Task.new using: applies refinements before first run") do
  r = nil
  done = false
  Task.new(using: [Ext1]) do
    r = "hello".shout
    done = true
  end
  while !done; Task.pass; end
  assert_equal "HELLO!!", r
end

assert("Independence after spawn: parent new using does not affect already-spawned child") do
  r = nil
  parent_done = false
  child_done = false
  Task.new do
    Task.current.using Ext1
    Task.new do
      begin
        r = "hello".shout
      rescue => e
        r = [e.class.to_s, e.message]
      end
      child_done = true
    end
    Task.current.using Ext2
    parent_done = true
  end
  while !parent_done || !child_done; Task.pass; end
  assert_equal "HELLO!!", r
end

assert("Task.new using: later refinements win") do
  r = nil
  done = false
  Task.new(using: [Ext1, Ext2]) do
    r = "hello".shout
    done = true
  end
  while !done; Task.pass; end
  assert_equal "HELLO??", r
end

assert("using on another task raises ArgumentError") do
  t = Task.new {}
  assert_raise(ArgumentError) { t.using Ext1 }
end

assert("using on dormant task raises TypeError") do
  t_done = Task.new {}
  while t_done.status != :DORMANT; Task.pass; end
  assert_raise(TypeError) { t_done.using Ext1 }
end

assert("active_refinements returns correct count") do
  r = nil
  done = false
  Task.new do
    Task.current.using Ext1
    Task.current.using IntExt
    r = Task.current.active_refinements.size
    done = true
  end
  while !done; Task.pass; end
  assert_equal 2, r
end

module KernelGreet
  refine Kernel do
    def greet
      "refined kernel"
    end
  end
end

class GreetHost
  def self.greet
    "host singleton"
  end
end

assert("Direct method on receiver class wins over refinement of an ancestor") do
  r_singleton = nil
  r_kernel = nil
  done = false
  Task.new do
    Task.current.using KernelGreet
    # GreetHost is a Class. Its singleton class chain eventually includes
    # Kernel (via Object). The refinement targets Kernel#greet, but
    # GreetHost has a direct singleton-class `greet`; that must win.
    r_singleton = GreetHost.greet
    # For a plain Object whose chain has no other `greet`, the refinement
    # must still apply when walking up to Kernel.
    r_kernel = Object.new.greet
    done = true
  end
  while !done; Task.pass; end
  assert_equal "host singleton", r_singleton
  assert_equal "refined kernel", r_kernel
end

assert("Mailbox pattern: apply extension delivered via Task::Queue") do
  mailboxes = {}
  mailboxes["worker"] = Task::Queue.new
  r = nil

  Task.new(name: "worker") do
    my_name = Task.current.name
    ext = mailboxes[my_name].pop
    mailboxes[my_name].close
    mailboxes.delete(my_name)
    Task.current.using ext
    r = "hello".shout
  end

  Task.new { mailboxes["worker"] << Ext1 }
  Task.run

  assert_equal "HELLO!!", r
end
