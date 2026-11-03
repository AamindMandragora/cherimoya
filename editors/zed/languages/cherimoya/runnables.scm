; a Run test button on every top-level test, the function under @test in a .toya file, whose name the task gets as ZED_CUSTOM_test, where a nested test has no button, since its name holds the function around it
(sourceFile
  (attribute
    name: (identifier) @_attribute
    target: (functionDeclaration
      name: (identifier) @run @test))
  (#eq? @_attribute "test")
  (#set! tag cherimoya-test))
