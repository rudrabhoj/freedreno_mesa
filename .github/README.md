# freedreno_mesa

This repository is a downstream copy of [Mesa](https://gitlab.freedesktop.org/mesa/mesa) with one correction for the
freedreno OpenGL driver. Freedreno is the Mesa driver for Qualcomm Adreno GPUs.

## Why this repository exists

1. The freedreno OpenGL driver has a memory leak on Adreno a7xx GPUs and all later GPUs.
2. We found the cause and made a correction. We tested the correction.
3. The server of the Mesa project did not let our new account send the correction. Refer to the section
   "Why the correction is not in Mesa".
4. Thus we publish the correction here. Other persons can use it, and the record stays available.

## The defect

On a7xx and later GPUs, the driver does not write the autotune fence to memory.
The list `fd_autotune::pending_results` does not become empty.
Each draw batch that uses autotune adds one `fd_batch_result` to the list, and the driver does not release it.

The result is that the memory of each OpenGL program increases continuously.
A compositor has the largest increase, because it draws all the time.

Measured on our system:

- After two days, the heap of the niri compositor was 16.24 GB.
- The list contained 17,646,737 entries. The oldest entry had fence 1.
- The increase was approximately 135 bytes for each draw batch.

### Cause

`process_results()` releases an entry only when `at->results->fence` is equal to or more than the fence of the entry.
The GPU must write this value.

`fd6_fence_write()` in `src/gallium/drivers/freedreno/a6xx/fd6_emit.h` calls:

    __event_write<CHIP>(cs, FD_CACHE_CLEAN, EV_WRITE_USER_32B, EV_DST_RAM, val, bo, offset);

`__event_write()` adds the address and the value only if `fd_gpu_events<CHIP>[event].needs_seqno` is true.
The table in `src/freedreno/common/freedreno_gpu_event.h` has these entries for `FD_CACHE_CLEAN`:

| GPU generation | Entry |
|---|---|
| a6xx | `{CACHE_FLUSH_TS, true}` |
| a7xx and later | `{CACHE_CLEAN, false}` |

Thus, on a7xx and later, the packet contains no address and no value.
`at->results->fence` stays at 0, and `process_results()` stops at the first entry.

### History

Mesa commit 1f41d5905950 ("freedreno/a6xx: Refactor CP_EVENT_WRITE emit", 2024-07-28) added `fd6_fence_write()`.
At that commit, the a7xx entry for `FD_CACHE_CLEAN` was already `{CACHE_CLEAN, false}`.
Thus the defect is in all Mesa releases that contain this commit.
On 2026-11-11, the defect was in Mesa 26.2.5, 26.2.6, 26.3.0 and `main`.

## The correction

On a7xx and later, `fd6_fence_write()` sends `CP_EVENT_WRITE7` with `CACHE_FLUSH_TS` and `.write_enabled = true`.
The packet contains the address and the value.
The Vulkan driver (turnip) writes its autotune fence in the same way: refer to `write_fence_cs()` in
`src/freedreno/vulkan/tu_autotune.cc`.
The a6xx path does not change.

The correction changes one function and adds 22 lines.

## Contents of this repository

| Item | Description |
|---|---|
| Branch `konig-26.2.5` | Mesa 26.2.5 (tag `mesa-26.2.5`) with the correction. This is the default branch. |
| Branch `main` | The Mesa `main` branch on the date of the copy (2026-11-11). It has no changes. |
| `konig/freedreno-autotune-fence-a7xx.patch` | The correction as a patch file. It applies to Mesa 26.2.5, 26.2.6 and 26.3.0. |
| `konig/leak-test.c` | The test program. |
| `.github/README.md` | This file. The Mesa `README.rst` is not changed. |

## How to use the correction

Use one of these two procedures.

Procedure A, with the patch file:

1. Get the Mesa source for your version.
2. Apply the patch: `patch -p1 < konig/freedreno-autotune-fence-a7xx.patch`.
3. Build and install Mesa with the usual procedure of your distribution.
4. Start your compositor and your programs again. A program loads the driver only when it starts.

Procedure B, with git:

1. Add this repository as a remote to your Mesa clone.
2. Do a cherry-pick of the commit "freedreno/a6xx: Write the autotune fence on a7xx and later".
3. Build and install Mesa.
4. Start your compositor and your programs again.

## How to do the test

`konig/leak-test.c` uses EGL surfaceless and one framebuffer object. It does not open a window.
Each cycle makes one batch with a clear and a draw, and one batch with a draw only.
The program measures the increase of its memory (RSS) for 300,000 batches. It also reads one pixel back.

    gcc -O1 -o leak-test konig/leak-test.c -lEGL -lGLESv2
    ./leak-test 150000

Results on our system:

| Driver | Memory increase for each batch | Pixel |
|---|---|---|
| Mesa 26.2.5, no correction | 134.9 bytes | correct |
| Mesa 26.2.5, with the correction | 0.2 bytes | correct |

With the correction, the private memory of niri stayed at 45 MiB to 46 MiB for two hours.
Before the correction, the increase was approximately 340 MB for each hour.
The kernel log showed no new GPU faults.

## Test environment

- Hardware: ASUS Zenbook A16 (UX3607OA), Snapdragon X2 Elite, Adreno X2-90 (a8xx).
- Operating system: Void Linux, aarch64, glibc. Kernel 7.3.0.
- Mesa: 26.2.5, built from the Void Linux package recipe.
- Compositor: niri 26.04 (GLES renderer).

## Limits

- We did the test on a8xx only. We did not do a test on a7xx hardware.
- We did not run the conformance test suite (CTS).
- This repository is not a general Mesa distribution. It contains one correction.

## Why the correction is not in Mesa

### What occurred

The Mesa project accepts a change only as a merge request on `gitlab.freedesktop.org`.
The merge request must come from a personal fork on that server.
The Mesa documentation tells you not to send patches by email.

We made an account on `gitlab.freedesktop.org` on 2026-11-11. The account could not make a fork.
The server showed this message:

> Limit reached. You cannot create projects in your personal namespace. Contact your GitLab administrator.

The message does not give the reason. It does not give the procedure to remove the limit.

### The procedure that the message does not show

We found the procedure later, in the wiki of the `freedesktop/freedesktop` project.
These are the steps for a person who wants to send a first correction to Mesa:

1. Make an account and confirm the email address. The account is an "external user".
   It can write issues. It cannot make a fork or a project.
2. Write a "User verification" issue in the `freedesktop/freedesktop` project.
   In the template, you must write why you want access. You must also accept two conditions.
3. Wait until an administrator approves the request with a label. Then a program removes the limits.
4. Make a fork of Mesa, push a branch, and open a merge request.
5. The test pipeline of your merge request fails with the message "Looks like you don't have enough privileges".
   Only group members can start a pipeline. A maintainer must start it for you.
6. You cannot merge. A person with developer access must give the merge request to the merge program.

freedesktop.org gives the reason for steps 1 to 3: spam, and abuse of the test computers for cryptocurrency mining.
Its wiki tells that there are approximately 50 spam accounts for each real account.

We examined the public list of "User verification" issues on 2026-11-11:

| Item | Value |
|---|---|
| Requests since the start | 2,870 |
| Requests that we examined (2026-10-02 to 2026-11-11) | 300 |
| Approved | 300 of 300 |
| Time to approval, median | 0.4 hours |
| Time to approval, 90% of the requests | 5.2 hours or less |
| Longest time | 78 hours |

Thus the path is open, and it is usually fast. We give these numbers because they are true,
and because our criticism must be correct.

We did not make the request. We publish the correction here.

### Our criticism

1. The server hides the procedure at the moment when the person needs it. The procedure is in a wiki of a
   different project. The message tells you only to contact an administrator. A person who does not find the
   wiki page stops there. We stopped there.
2. Each new person is a suspect until an administrator makes a decision. You must write a reason and accept
   conditions before the project lets you give it free work.
3. The check does not select. In our sample, the administrators approved 300 of 300 requests. A check that
   always gives the answer "yes" is a ceremony. Its effects are a delay for each real contributor, manual work
   for the administrators, and the loss of the persons who stop at the message.
4. The limit is in the incorrect position. The wiki tells that the spam is in issues and in user profiles.
   A new account can continue to write issues. The mining abuse used the test pipelines, and the pipelines have a
   different lock that also applies to verified users. Spam in the issues of forks was a third problem, and
   forks now have issues set to off by default. Thus the fork limit stops the one operation that is not spam:
   to offer a change to the code.
5. There is only one path. Mesa does not accept patches by email, and a merge request is not possible without a fork.
6. After the approval, the new contributor is not equal. The contributor cannot start the tests for their own change.

### Our opinion

This section is our opinion. It is not polite.

We accept that spam is a real problem. Spam is not a reason to handle each engineer as a suspect.

This is the attitude of a control freak. The project wants permission first and work second.
You must ask, give a reason, accept conditions and wait for an official. Only then can you give the project a gift.
We call that fascistic. The word is strong, and we use it deliberately: a system that trusts no person without a
stamp from an official has the same reflex, in a small size.

The result shows the cost. The defect has been in each release since July 2024.
It uses gigabytes of memory on each computer with an a7xx or later GPU that runs an OpenGL compositor.
The correction has 22 lines, and it is tested. On the day that we had it, the first answer of the server was "no".

A project that depends on contributors must say "yes" first and remove the spam later.
freedesktop.org does the opposite, and it lets its users pay for it.

### What we want

- A new account must be able to open one merge request to an existing project without a request to an administrator.
- If a limit stays, the message must give the reason and a link to the procedure.
- A contributor must be able to send a patch by a second path, for example by email, when the first path is closed.

## Permission to use this work

Any person can send this correction to the Mesa project. You do not have to ask us.
The commit message contains the `Fixes:` line that the Mesa rules specify.

If you send it, the Mesa rules for contributors apply to you (`docs/submittingpatches.rst`).
We used an AI tool (a large language model) for the analysis, the correction and this text.
For such work, the Mesa rules specify a disclosure tag (`Assisted-by: LLM` or `Generated-by: LLM`)
and a commit message in your own words.

When a Mesa release contains a correction for this defect, we will write that here and stop the maintenance of this repository.

## License

Mesa has its own licenses. Refer to `docs/license.rst`. Our changes have the same license as the files that they change (MIT).
