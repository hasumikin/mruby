MRuby::Build.new('task-refinements') do |conf|
  conf.toolchain
  conf.build_mrbc_exec

  if Gem.win_platform?
    conf.gem core: 'hal-win-task'
  else
    conf.gem core: 'hal-posix-task'
  end

  conf.gem core: 'mruby-bin-mruby'
  conf.gem core: 'mruby-task-refinements'

  conf.enable_test
end
