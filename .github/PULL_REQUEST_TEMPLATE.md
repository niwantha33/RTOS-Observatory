## Related work

Phase issue: #  
Work package(s): W  
Bug issue(s) (if any): #

## Scope and implementation

Describe the source/config changes and explicitly list out-of-scope features.

## Public repository privacy check

- [ ] Only material explicitly approved for public release is included.
- [ ] No confidential product concepts, non-public research, credentials, personal/customer data, source dumps, or unredacted logs are included in commits, PR text or attachments.
- [ ] Synthetic or sanitised fixtures are used; no private files staged or force-added despite `.gitignore`.

## Validation

- [ ] Native build and CTest on supported host(s) (include OS/compiler/result)
- [ ] Malformed/incomplete/lost trace failure tests (when applicable)
- [ ] Input identity, timestamps, loss and unknown states preserved
- [ ] No second debug-session owner / silent target halt, resume or reset
- [ ] Physical target/probe tests included **if** hardware support is claimed (list exact setup)
- [ ] Docs and `docs/PROJECT_STATUS.md` updated to reflect verified reality

## Evidence

Build/CI logs:  
Saved traces or regression fixtures:  
Board/probe/firmware/RTOS/toolchain and trace overhead (if applicable):  
Known limitations / missing qualification:  

## Release claim

- [ ] Prototype or offline/synthetic only
- [ ] Hardware-tested under exactly stated conditions
- [ ] Broadly qualified production support (attach matrix)

Do not mark the linked package complete until merged and accepted.
