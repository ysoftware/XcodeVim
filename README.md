## Parser of .xcactivitylog files

Provides error messages from Xcode, ready to be used with VIM.

### Build and Run

```bash
$ clang -o app.exe src/main.c -lz -O3

$ ./app.exe MyApp
$ ./app.exe MyApp -d
```

### Explanation

Latest modified .xcactivitylog file found inside will by analysed.
Project name argument is required to narrow the scope of search inside derived data:
`~/Library/Developer/Xcode/DerivedData/[project_name]*/Logs/Build/`
#
In VIM, you can populate the quickfix buffer with error output by calling `:cexpr` or another command.

I put this in my .vimrc to bind this command to to **te**:
```vimL
nnoremap te :cexpr system('~/XcodeVim/app.exe MyApp')<CR>:copen<CR><CR>
```

Example of what you get in quickfix:
```
file:///Users/username/MyFile.swift:50:32: Value of type 'MyApp' has no member 'bork'
file:///Users/username/MyOtherFile.swift:20:12: 
```
#
Pass flag -d to dump the whole parsed log as log-dump.txt.
Double values are not parsed currently, but if anybody wants to implement it, it should be quite easy.
Here is an example of the output:
```
[type: "int", value: 11]
[type: "className", index: 1, value: "IDEActivityLogSection"]
[type: "classInstance", value: "IDEActivityLogSection"]
[type: "int", value: 0]
[type: "string", length: 39, value: "Xcode.IDEActivityLogDomainType.BuildLog"]
[type: "string", length: 30, value: "Build C24MobileSimOnly-Example"]
[type: "string", length: 30, value: "Build C24MobileSimOnly-Example"]
[type: "double", value: not parsed]
[type: "double", value: not parsed]
[type: "array", count: 25]
[type: "classInstance", value: "IDEActivityLogSection"]
[type: "int", value: 1]
```
