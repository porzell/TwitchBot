#pragma once
#ifndef _TWITCH_IRC_CLIENT_H_
#define _TWITCH_IRC_CLIENT_H_

#include <string>
#include <vector>
#include <deque>


#include "ofMain.h"
#include "ofxNetwork.h"

enum class TwitchIRCClientError
{
	Unknown,
	Unable_To_Connect,
	Unable_To_Auth
};

struct IRCMessage
{
	uint64_t timestamp;
	std::string username;
	std::string message;
};

class TwitchIRCClient;

// Define a callback function prototype.
typedef void (*TWITCH_IRC_MESSAGE_CALLBACK)(IRCMessage*,TwitchIRCClient*);

struct TwitchMessageListener
{
	std::string search;
	TWITCH_IRC_MESSAGE_CALLBACK handler;

	TwitchMessageListener() {};
	TwitchMessageListener(std::string search, TWITCH_IRC_MESSAGE_CALLBACK handler)
	{
		this->search = search;
		this->handler = handler;
	}
};

class TwitchIRCClient
{
	private:
		ofxTCPClient mTCP;

		std::string hostname;
		std::string username;
		std::string oauth;

		int port;

		std::string channelname;

		std::vector<IRCMessage> messages;

		uint64_t lastMessageTimestamp = 0;
		uint64_t minMessageInterval = 2000;

		std::deque<std::string> outgoingMessageQueue;

		std::map<std::string, TwitchMessageListener> messageListeners;

		static IRCMessage* getIRCMessage(std::string sentence)
		{
			IRCMessage *out = nullptr;

			char *buf = new char[sentence.length() + 1];

			strcpy(buf, (char*)sentence.c_str());

			char *ptr = buf;
			char *next_token = nullptr;
			char *token = strtok_s(ptr, " ", &next_token);

			std::vector<char*> v;

			while (token)
			{
				v.push_back(token);
				token = strtok_s(NULL, " ", &next_token);
			}

			if (v.size() > 3)
			{
				if (strcmp(v[1], "PRIVMSG") == 0)
				{
					out = new IRCMessage();

					// To get username we need to read between the : and !
					strtok(v[0], "!");

					// +1s basically are a horrible hack to ignore the starting colon.

					out->message = (char*)((v[3] - buf) + sentence.c_str()) + 1;
					out->username = v[0] + 1;
				}
			}

			delete buf;

			return out;
		}

	public:
		TwitchIRCClient() {};

		TwitchIRCClient(std::string hostname, int port, std::string username, std::string oauth, uint64_t sendinterval = 2000)
		{
			minMessageInterval = sendinterval;

			connect(hostname, port, username, oauth);
		};

		~TwitchIRCClient()
		{
			mTCP.close();
		}


		inline uint64_t getMinMessageInterval() { return minMessageInterval; }

		inline void setMinimumMessageInterval(uint64_t millis)
		{
			minMessageInterval = millis;
		}

		void connect(std::string hostname, int port, std::string username, std::string oauth)
		{
			this->hostname = hostname;
			this->username = username;
			this->oauth = oauth;
			this->port = port;

			mTCP.setMessageDelimiter("\r\n");

			if (!mTCP.setup(hostname, port))
			{
				throw TwitchIRCClientError::Unable_To_Connect;
			}

			sendRaw("PASS " + oauth,true);
			sendRaw("USER " + username, true);
			sendRaw("NICK " + username, true);
		}

		// Returns true if sent.
		bool sendRaw(std::string msg, bool disobeyTimeout = false, bool shouldQueue = false)
		{
			if (mTCP.isConnected())
			{
				if (disobeyTimeout || ofGetElapsedTimeMillis() - lastMessageTimestamp > minMessageInterval)
				{
					mTCP.sendRaw(msg + "\r\n");

					lastMessageTimestamp = ofGetElapsedTimeMillis();

					return true;
				}
				else if (shouldQueue)
				{
					outgoingMessageQueue.emplace_back(msg);
				}
			}
			else
				throw TwitchIRCClientError::Unable_To_Connect;

			return false;
		}

		void joinChannel(std::string channelname)
		{
			this->channelname = channelname;

			sendRaw("JOIN #" + channelname, true);
		};

		void sendChannelMsg(std::string msg, bool disobeyTimeout = false, bool shouldQueue = false)
		{
			sendRaw("PRIVMSG #" + channelname + " :" + msg, disobeyTimeout, shouldQueue);
		}

		void addTwitchMessageHandler(std::string name, std::string search, TWITCH_IRC_MESSAGE_CALLBACK handler)
		{
			messageListeners[name] = TwitchMessageListener(search, handler);
		}

		inline void removeTwitchMessageHandler(std::string name)
		{
			messageListeners.erase(name);
		}

		inline void clearTwitchMessageHandlers()
		{
			messageListeners.clear();
		}

		virtual void update()
		{
			if (mTCP.isConnected())
			{
				std::string msg;
				
				//============================
				// Recieve everything we can.
				//============================
				msg = mTCP.receive();

				uint64_t timestamp = ofGetElapsedTimeMillis();

				while (msg.length() > 0)
				{
					printf("%s", msg.c_str());

					// Allow ping response.
					if (msg.substr(0, 4) == "PING")
					{
						//Send a PONG back.
						msg[1] = 'O';

						sendRaw(msg, true);
					}
					else
					{
						// Do we have a channel message?
						IRCMessage *message = nullptr;

						if (message = getIRCMessage(msg))
						{
							bool sent = false;

							// Set message timestamp.
							message->timestamp = timestamp;

							// See if our message triggers any message listeners.
							for (auto it = messageListeners.begin(); it != messageListeners.end(); ++it)
							{
								auto listener = it->second;
								
								// Make sure we don't call any all-listeners.
								if (listener.search.size() > 1 && 
										it->first.size() > 1 &&
										message->message.find(listener.search) != -1)
								{
									listener.handler(message,this);
									sent = true;
								}
							}

							// If we didn't handle this message elsewhere and we have an all-listener, hand it over.
							auto emptyIterator = messageListeners.find("");

							if (emptyIterator != messageListeners.end())
							{
								if (!sent)
								{
									messageListeners[""].handler(message,this);
								}
							}

							delete message;
						}
					}

					msg = mTCP.receive();
				}

				//============================
				// Check our sending queue
				//============================

				if (outgoingMessageQueue.size() > 0)
				{
					auto next = outgoingMessageQueue.front();

					if (sendRaw(next))
						outgoingMessageQueue.pop_front();
				}
			}
			else
				throw TwitchIRCClientError::Unable_To_Connect;
		}
};

#endif //_TWITCH_IRC_CLIENT_H_
