# Chaos Poker Take-Home

Build a bot that plays Chaos Poker.

## Task

Implement a bot that communicates with the engine over `stdin` / `stdout` using the protocol in [RULES.md](RULES.md).

The code in this repository is provided as a **test harness** for local development.

If the harness implementation or the message-flow example appear to deviate from [RULES.md](RULES.md), the rules in [RULES.md](RULES.md) should be treated as authoritative.

You may use any language, as long as your bot can be launched from the command line and responds within the time limit.

## Evaluation

Your bot should aim to maximise **match wins**.

We will run the submitted bots through repeated offline evaluations and rank them based on their performance in those tournaments.

We may also assess submissions qualitatively, including the overall quality of the approach and implementation.

## Running the Test Harness

Build the engine and sample bots:

```bash
make
```

Run a sample 3-player match with hand history output:

```bash
./chaos_poker --history 1000 5 15 25 50 ./bots/example_bot ./bots/random_bot ./bots/random_bot
```

## Submission

Create a private GitHub repository for your work.

Your repository should contain:

- your bot source code
- a top-level `README.md`

Submission deadlines:

- Invite `tk-machine-user` with read access by Sunday, April 12, 2026, 23:59 IST so any access or permissions issues can be resolved before the final deadline.
- Follow up by email with your repository URL and your GitHub username after you have sent the GitHub invitation.
- Do not make any further changes to your repository after Monday, April 13, 2026, 23:59 IST.

In your repository `README`, include:

- your name
- the exact command we should use to launch your bot
- how to build your bot
- how to run your bot
- a short note describing your strategy and any tradeoffs

## Questions and Clarifications

If you have questions about the exercise, please raise them as GitHub issues in this repository.

Because this exercise runs over the weekend, you should not depend on receiving a timely answer to any question and should be prepared to make reasonable assumptions and work with uncertainty.

Any official clarifications to the rules will be summarised in this README so you do not need to inspect the commit history to find them.

## Clarifications

- Response timeout: the official time limit is now 10 ms per decision, and the provided test harness has been updated accordingly.
