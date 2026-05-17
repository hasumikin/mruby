module Kernel
  private

  def using(mod)
    Task.current.using(mod)
  end

  def unusing(mod)
    Task.current.unusing(mod)
  end
end
