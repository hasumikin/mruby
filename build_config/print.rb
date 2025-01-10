MRuby::Build.new do |conf|
  conf.toolchain :gcc

  conf.gem core: "mruby-bin-mruby"
  conf.gem core: "mruby-print"

#  conf.disable_presym
end

