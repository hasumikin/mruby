MRuby::Gem::Specification.new('mruby-task-refinements') do |spec|
  spec.license = 'MIT'
  spec.authors = 'PicoRuby developers'
  spec.summary = 'Task-scoped dynamic refinements for mruby-task'

  spec.add_dependency 'mruby-task'

  spec.build.defines << 'MRB_USE_TASK_REFINEMENTS'
end
