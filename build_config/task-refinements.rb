MRuby::Build.new('task-refinements') do |conf|
  conf.toolchain
  conf.build_mrbc_exec

  if Gem.win_platform?
    conf.gem core: 'hal-win-task'
  else
    conf.gem core: 'hal-posix-task'
  end

  conf.gem core: 'mruby-bin-mruby'
  # mruby-task is auto-loaded by the HAL gem dependency. Enable the
  # task-scoped refinements built into mruby-task.
  conf.cc.defines << 'MRB_USE_TASK_REFINEMENTS'

  conf.enable_test
end
