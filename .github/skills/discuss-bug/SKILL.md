---
name: discuss-bug
description: 'Discuss a bug without modifying the code base.'
disable-model-invocation: true
user-invocable: true
---

Discuss the following bug with user. DO NOT modify any code until the user gives explicit permission.
This process is:
1. User describes the symptoms in detail, optionally any specific code to look at.
2. Examine relevant code and analyze.
3. Ask clarifying questions if needed or ask user to test anything else that might help. Imagine you are tech support over phone.
4. Come up with a potential fix and ask user to give go-ahead.
5. When user gives permission, make the changes and ask user to re-test.
6. If fix does not work, repeat from step 1.
