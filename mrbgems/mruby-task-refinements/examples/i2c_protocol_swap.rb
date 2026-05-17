# i2c_protocol_swap.rb
#
# Demonstrates task-scoped refinements: two tasks talk to two different I2C
# peripherals that use incompatible register layouts. Each task activates its
# own refined I2C driver so that I2C#read_reg returns the correct value for
# its peripheral without touching the other task's behaviour.
#
# This example is intentionally self-contained and does not require real I2C
# hardware; it stubs the driver with simple string returns so it can be run
# on any host with mruby-task and mruby-task-refinements enabled.

# Minimal stub representing a shared I2C bus object
class I2C
  def read_reg(addr, reg)
    "raw(addr=#{addr} reg=#{reg})"
  end

  def write_reg(addr, reg, val)
    # no-op stub
  end
end

BUS = I2C.new

# Refinement for Peripheral A (e.g. an accelerometer with a big-endian layout)
module AccelDriver
  refine I2C do
    def read_reg(addr, reg)
      raw = super_raw(addr, reg)  # would call HAL in real code
      "AccelDriver:#{raw}"
    end

    # In real code this would issue the actual HAL call.
    # Here we just produce a predictable string.
    def super_raw(addr, reg)
      "BE(#{addr},#{reg})"
    end

    def read_accel_xyz(addr)
      x = super_raw(addr, 0x00)
      y = super_raw(addr, 0x02)
      z = super_raw(addr, 0x04)
      [x, y, z]
    end
  end
end

# Refinement for Peripheral B (e.g. a pressure sensor with a little-endian layout)
module PressureDriver
  refine I2C do
    def read_reg(addr, reg)
      raw = super_raw(addr, reg)
      "PressureDriver:#{raw}"
    end

    def super_raw(addr, reg)
      "LE(#{addr},#{reg})"
    end

    def read_pressure_pa(addr)
      raw = super_raw(addr, 0x06)
      "#{raw}Pa"
    end
  end
end

done_accel    = false
done_pressure = false

accel_result    = nil
pressure_result = nil

# Task A: talks to the accelerometer using AccelDriver refinements
Task.new(name: "accel", using: [AccelDriver]) do
  3.times do |i|
    xyz = BUS.read_accel_xyz(0x18)
    accel_result = xyz
    puts "[accel ##{i}] x=#{xyz[0]} y=#{xyz[1]} z=#{xyz[2]}"
    Task.sleep 10
  end
  done_accel = true
end

# Task B: talks to the pressure sensor using PressureDriver refinements
Task.new(name: "pressure", using: [PressureDriver]) do
  3.times do |i|
    pa = BUS.read_pressure_pa(0x76)
    pressure_result = pa
    puts "[pressure ##{i}] #{pa}"
    Task.sleep 15
  end
  done_pressure = true
end

while !done_accel || !done_pressure
  Task.pass
end

puts ""
puts "Final accel reading:    #{accel_result.inspect}"
puts "Final pressure reading: #{pressure_result.inspect}"
puts "Done."
